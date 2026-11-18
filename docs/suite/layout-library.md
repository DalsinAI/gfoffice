# GFOffice on gfopenlayout.library

GFOffice's programs lay out and draw their pages with GFOpenLayout. Until November 2026 each program compiled
GFOpenLayout's source in, so every program carried its own copy. They now open `gfopenlayout.library`, the one copy
the OS already has (Dale, 17 November 2026: "they should all be converted to it now"). No new library is made for
this.

## What a converted program needs

- `gfopenlayout.library` 24 or later. Version 24 carries the document calls by their C prototypes (gfopenlayout's
  README, "The document calls (24)"), so a program's source does not change: it still calls `ol_document_new`,
  `ol_layout_pages`, `ol_draw_page` and the rest.
- The library is part of the OS (the GFOpenKick ROM, or `LIBS:`). GFOffice does not build or ship its own copy: a
  copy on disk does not replace an older one in the ROM, and the OS's build has the theme reader that a GFOffice
  build would lack. A release of a converted program therefore names OS builds with library 24 as what it needs.
- On an older library the program stops at its first layout call with RETURN_FAIL and the line
  `This needs gfopenlayout.library 24 or later in LIBS:`.

## How the build does it

`build-os3.sh` has two lists. `$OL_C` is GFOpenLayout's source, for the programs not yet converted. `$OL_LIB` is
GFOpenLayout's link sources (`library/link/gfopenlayout_link.c`, `gfopenlayout_link_core.c` and
`gfopenlayout_link_base.c`): the same C calls, each made through the library, and the start-up code that opens it.
A converted program's compile names `$OL_LIB` where it named `$OL_C`. `deps/openamigalayout.ref` pins a
GFOpenLayout that has them.

`tests/layout/check_library_calls.sh` (`make layout-library-calls-check`, part of `make test` and `make check`)
holds each converted program to that: its compile in `build-os3.sh` names `$OL_LIB` and never `$OL_C`, and every
GFOpenLayout call its files make is one the link sources give. A call the library lacks shows there on the host,
before an Amiga link would fail on it.

## The steps

| Step | What | State |
|---|---|---|
| 1 | gfopenlayout.library 24: the document calls (gfopenlayout #77, master d622316a) | done |
| 2 | Build support, and GFOpenWrite | this change |
| 3 | GFOpenPresentation, GFOpenPressmaster, GFOpenBase's preview, OWConvert | to do |
| 4 | The drawing layer (`oldraw_*`) into the library, then GFOpenDiag and GFOpenProject | to do |
| 5 | Page drawing in owfpub.library and owfpresent.library through the library | to do |

A program is added to `programs` in `tests/layout/check_library_calls.sh` in the step that converts it.

## Lab

Each converted program is owed a lab pass on an OS build with library 24 before it is released: open a document,
page through it, edit, print to PDF, and compare with the same build before the step.
