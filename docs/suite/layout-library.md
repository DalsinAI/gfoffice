
# GFOffice on gfopenlayout.library

GFOffice's programs lay out and draw their pages with GFOpenLayout. Until November 2026 each program compiled
GFOpenLayout's source in, so every program carried its own copy. They now open `gfopenlayout.library`, the one
copy the OS has (Dale, 17 November 2026: "they should all be converted to it now"). No new library is made for
this.

## One way: the library

There is no switch. An Amiga build of a converted program is GFOpenLayout's link sources, the same C calls made
through `gfopenlayout.library`, and nothing else of GFOpenLayout (Dale, 18 November 2026: "no switch, remove all
the dead code and lock in just openlayout"). While the steps went in, `build-os3.sh` had an `OPENLAYOUT` setting
that chose between the source and the library; it and the compiled-in way are gone from the Amiga builds. The
host tests still compile GFOpenLayout's source, because a host has no Amiga library to open.

## What a program needs

- The page programs need `gfopenlayout.library` 24 or later. Version 24 carries the document calls by their C
  prototypes (gfopenlayout's README, "The document calls (24)"), so a program's source did not change: it still
  calls `ol_document_new`, `ol_layout_pages`, `ol_draw_page` and the rest.
- GFOpenDiag and GFOpenProject need 25, which carries the drawing layer (`oldraw_*`, `gfopenlayout_draw.h`).
- The library is part of the OS (the GFOpenKick ROM, or `LIBS:`). An OS whose ROM has an older one takes a newer
  copy from `LIBS:` at the first open after a boot (amicradle-guest, common/openramlib/README.md, "A newer disk
  copy wins"). See "Systems with an older library" below.

## Starting on an older library

`app/ow_layoutlib.c` is the one place a program opens the library. The program calls `ow_layout_open()` in its
start-up, with its other libraries and before it opens a window. When that answers 0 (no library, or one older
than the program needs) the program shows a requester, "GFOpenWrite needs gfopenlayout.library 24 or later in
LIBS:.", and ends with a failure, with nothing left open. A requester, because a program started from Workbench
has no Shell to print to. `ow_layout_close()` closes it on the way out.

The program has `OpenLayoutBase` of its own this way, so GFOpenLayout's own start-up code
(`gfopenlayout_link_base.c`, which opens version 10 and prints to a Shell only) is not linked.

## How the build does it

`build-os3.sh` sets four names, and stops when the pinned GFOpenLayout lacks a link source:

| Name | What it is |
|---|---|
| `$OL_PAGES` | `library/link/gfopenlayout_link.c`, `gfopenlayout_link_core.c`, `app/ow_layoutlib.c` |
| `$OL_PAGES_FLAGS` | `-DOW_LAYOUT_LIBRARY` |
| `$OL_DRAW` | `library/link/gfopenlayout_link_draw.c`, `app/ow_layoutlib.c` |
| `$OL_DRAW_FLAGS` | `-DOW_LAYOUT_LIBRARY -DOW_LAYOUT_DRAW` (ask for 25) |

A page program's compile names `$OL_PAGES` and `$OL_PAGES_FLAGS`; GFOpenDiag and GFOpenProject name `$OL_DRAW`
and `$OL_DRAW_FLAGS`. `deps/openamigalayout.ref` pins a GFOpenLayout that has the link sources.

`tests/layout/check_library_calls.sh` (`make layout-library-calls-check`, part of `make test` and `make check`)
holds each program to that: its compile names `$OL_PAGES` or `$OL_DRAW`; every GFOpenLayout call its files make
is one the link sources give, so a call the library lacks shows on the host before an Amiga link would fail on
it; and its main file asks `ow_layout_open()` and ends with a failure when that answers 0. It also holds the
build to the one way: it fails when `build-os3.sh`, `build-amiga.sh` or a file in `build.d/` names GFOpenLayout's
source directories, the drawing layer's Amiga flag, or an `OPENLAYOUT` setting.

`tools/owc_deck.c` keeps one `#ifdef OW_LAYOUT_LIBRARY`: the host's OWConvert compiles the source and has no
library to ask for.

## The steps

| Step | What | State |
|---|---|---|
| 1 | gfopenlayout.library 24: the document calls (gfopenlayout #77, master d622316a) | done |
| 2 | Build support, and GFOpenWrite (gfoffice #61) | done |
| 3 | GFOpenPresentation, GFOpenPressmaster, GFOpenBase's preview, OWConvert, the 1.x GFOpenWrite window | done |
| 4 | The drawing layer into the library as version 25 (gfopenlayout #78, master 56a0f09b), then GFOpenDiag and GFOpenProject | done |
| 5 | owfpub.library and owfpresent.library: nothing to convert. They carry no GFOpenLayout code (build-library.sh compiles none); their pages are drawn by the programs (src/deck, src/pub), converted in step 3 | not needed |
| 6 | The library only: the switch and the compiled-in way removed from the Amiga builds; gfopenlayout.library 25 built into the package, for systems whose ROM has an older one | this change |

A program is added to `programs` in `tests/layout/check_library_calls.sh` when it is converted.

## The programs

| Program | Asks for the library | Says, when it is missing or older |
|---|---|---|
| GFOpenWrite (and the 1.x window) | at start-up (24) | a requester, then ends |
| GFOpenPresentation, GFOpenPressmaster | at start-up (24) | a requester, then ends |
| GFOpenBase | at start-up (24: its report preview's pages) | a requester, then ends |
| OWConvert | only when a presentation is converted to PDF (24) | a line in the Shell; every other conversion runs without the library |
| GFOpenDiag, GFOpenProject | at start-up (25: their canvas and views) | a requester, then ends |
| GFOpenDrawOL (built from the gfopendraw pinned in `deps/gfopendraw.ref`) | at start-up (10: its window's GFOpenLayout UI) | a requester from Workbench, a line in a Shell, then ends |

GFOpenProject's printing draws on a target of its own (`oldraw_new` with the program's calls): those calls are
made from the library on the program's task.

## Systems with an older library

The ROM of today's OS images has library 23, so on its own such a system would show the requester. Until the ROM
carries 25 (asked of Team 5), GFOffice carries the library: `build-amiga.sh` builds `gfopenlayout.library` from the
GFOpenLayout pinned in `deps/openamigalayout.ref`, with GFOpenGadTools' theme reader at the commit
`deps/gfopengadtools.ref` names and at `-Os` (as the OS's own is built), into `library/` beside GFOffice's own libraries, and
the package installs it to `LIBS:` with them. The OS's loader starts the newer disk copy at the first open after a
boot, so an install on such a system needs a restart before the programs start. The start-up requester stays for
a system where the copy is missing or the restart has not happened.

The check holds this too: both refs are commits, `build-amiga.sh` builds the library with the theme reader at `-Os`, and
the GFOpenLayout it is given builds a library no older than `app/ow_layoutlib.h` asks for.

## Lab

Each program is owed a lab pass on an OS with library 25 (in the ROM, or from `LIBS:` over a ROM with 23): open
a document, page through it, edit, print to PDF, and compare with the build before its step. The pass over a ROM
with 23 also proves the loader takes the newer copy.
