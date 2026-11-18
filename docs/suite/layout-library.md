# GFOffice on gfopenlayout.library

GFOffice's programs lay out and draw their pages with GFOpenLayout. Until November 2026 each program compiled
GFOpenLayout's source in, so every program carried its own copy. They now open `gfopenlayout.library`, the one copy
the OS already has (Dale, 17 November 2026: "they should all be converted to it now"). No new library is made for
this.

## The switch

A converted program can be built either way, and `build-os3.sh`'s `OPENLAYOUT` chooses for all of them:

| `OPENLAYOUT` | What the program is | Where it runs |
|---|---|---|
| `source` (the default now) | As before: GFOpenLayout's source compiled in | Every OS image |
| `library` | GFOpenLayout's link sources: the same C calls, made through `gfopenlayout.library` | An OS with library 24 or later |

The default stays `source` until the OS's ROM carries library 24 and the lab has passed on it. Releases are cut
from green main, and a `library` build cannot start on an OS image with library 23, so merging a conversion step
must not change what a release is. The default is then flipped to `library` in a one-line change of its own, and
a last change removes the `source` way from the Amiga builds (the host tests keep compiling the source).

## What a library build needs

- `gfopenlayout.library` 24 or later. Version 24 carries the document calls by their C prototypes (gfopenlayout's
  README, "The document calls (24)"), so a program's source does not change: it still calls `ol_document_new`,
  `ol_layout_pages`, `ol_draw_page` and the rest.
- The library is part of the OS (the GFOpenKick ROM, or `LIBS:`). GFOffice does not build or ship its own copy: a
  copy on disk does not replace an older one in the ROM, and the OS's build has the theme reader that a GFOffice
  build would lack.

## Starting on an older library

`app/ow_layoutlib.c` is the one place a program opens the library. The program calls `ow_layout_open()` in its
start-up, with its other libraries and before it opens a window. When that answers 0 (no library, or one older
than 24) the program shows a requester, "GFOpenWrite needs gfopenlayout.library 24 or later in LIBS:.", and ends
with a failure, with nothing left open. A requester, because a program started from Workbench has no Shell to
print to. `ow_layout_close()` closes it on the way out. In a `source` build the two do nothing, so the program's
code is the same either way.

The program has `OpenLayoutBase` of its own this way, so GFOpenLayout's own start-up code
(`gfopenlayout_link_base.c`, which opens version 10 and prints to a Shell only) is not linked.

## How the build does it

`build-os3.sh` sets `$OL_PAGES` and `$OL_PAGES_FLAGS` from `OPENLAYOUT`: the link sources
(`library/link/gfopenlayout_link.c`, `gfopenlayout_link_core.c`) and `-DOW_LAYOUT_LIBRARY`, or the source (`$OL_C`)
and nothing; `app/ow_layoutlib.c` is in both. A converted program's compile names `$OL_PAGES` and
`$OL_PAGES_FLAGS` where it named `$OL_C`. `deps/openamigalayout.ref` pins a GFOpenLayout that has the link
sources.

`tests/layout/check_library_calls.sh` (`make layout-library-calls-check`, part of `make test` and `make check`)
holds each converted program to that: its compile names `$OL_PAGES` and never `$OL_C`; every GFOpenLayout call
its files make is one the link sources give, so a call the library lacks shows on the host before an Amiga link
would fail on it; its main file asks `ow_layout_open()` and ends with a failure when that answers 0; and
`ow_layoutlib.c` in a `source` build asks for nothing. It prints the default `OPENLAYOUT`.

## The steps

| Step | What | State |
|---|---|---|
| 1 | gfopenlayout.library 24: the document calls (gfopenlayout #77, master d622316a) | done |
| 2 | Build support, and GFOpenWrite (gfoffice #61) | done |
| 3 | GFOpenPresentation, GFOpenPressmaster, GFOpenBase's preview, OWConvert, the 1.x GFOpenWrite window | done |
| 4 | The drawing layer (`oldraw_*`) into the library as version 25 (gfopenlayout #78, master 56a0f09b), then GFOpenDiag and GFOpenProject | this change |
| 5 | owfpub.library and owfpresent.library: nothing to convert. They carry no GFOpenLayout code (build-library.sh compiles none); their pages are drawn by the programs (src/deck, src/pub), converted in step 3 | not needed |
| 6 | After the ROM pin and the lab pass: the default becomes `library` | to do |
| 7 | The `source` way removed from the Amiga builds | to do |

A program is added to `programs` in `tests/layout/check_library_calls.sh` in the step that converts it. Every
step's Amiga build is made both ways.

## The programs

| Program | Asks for the library | Says, when it is missing or older |
|---|---|---|
| GFOpenWrite (and the 1.x window) | at start-up | a requester, then ends |
| GFOpenPresentation, GFOpenPressmaster | at start-up | a requester, then ends |
| GFOpenBase | at start-up (its report preview's pages) | a requester, then ends |
| OWConvert | only when a presentation is converted to PDF | a line in the Shell; every other conversion runs without the library |
| GFOpenDiag, GFOpenProject | at start-up (library 25: their canvas and views) | a requester, then ends |

After step 3 no compile in `build-os3.sh` or `build.d/` names `$OL_C` itself, and the check fails if one does.
After step 4 no Amiga compile takes GFOpenLayout's source except through the switch (`$OL_PAGES`, `$OL_DRAW`).
GFOpenDiag and GFOpenProject draw with GFOpenLayout's drawing layer (`oldraw_*`, `gfopenlayout_draw.h`), which
came into the library in version 25. They name `$OL_DRAW` and `$OL_DRAW_FLAGS` where they named `draw/oldraw.c`
and `amiga/oldraw_amiga.c`: with `OPENLAYOUT=library` that is the library's link source
(`gfopenlayout_link_draw.c`) and `app/ow_layoutlib.c` asking for 25 (`OW_LAYOUT_DRAW`), with `source` the two
files as before. GFOpenProject's printing draws on a target of its own (`oldraw_new` with the program's calls):
with the library those calls are made from the library on the program's task. So the OS's library has to be 25
for these two, where the page programs need 24.

## Lab

Each converted program is owed a lab pass, built with `OPENLAYOUT=library`, on an OS build with library 24 (25 for GFOpenDiag and GFOpenProject)
before the default is flipped: open a document,
page through it, edit, print to PDF, and compare with the same build before the step.
