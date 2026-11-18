/* GFOpenLayout for a GFOffice program: gfopenlayout.library, the one copy the system has (build-os3.sh,
 * docs/suite/layout-library.md). A program calls ow_layout_open() in main() before it opens anything of its
 * own, and when it answers 0 says so (OW_LAYOUT_NEEDS: a requester from Workbench) and ends with RETURN_FAIL;
 * it calls ow_layout_close() on its way out.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#ifndef OW_LAYOUTLIB_H
#define OW_LAYOUTLIB_H

/* what the program's requester names: "GFOpenWrite needs " OW_LAYOUT_NEEDS " in LIBS:." */
#ifdef OW_LAYOUT_DRAW
/* a program that draws with GFOpenLayout drawing (build-os3.sh's $OL_DRAW_FLAGS): the library version with
 * gfopenlayout_draw.h's calls (OLDraw_NewRastport on) */
#define OW_LAYOUT_LIBRARY_VERSION 25
#define OW_LAYOUT_NEEDS "gfopenlayout.library 25 or later"
#else
/* the library version with the document calls by their C prototypes (OLAPI_DocumentNew on) */
#define OW_LAYOUT_LIBRARY_VERSION 24
#define OW_LAYOUT_NEEDS "gfopenlayout.library 24 or later"
#endif

/* 1 when GFOpenLayout's calls can be made: the library is open at OW_LAYOUT_LIBRARY_VERSION or later. 0 when
 * it is missing or older; nothing is left open then. */
int ow_layout_open(void);
/* Closes what ow_layout_open opened. Safe to call when it answered 0, or twice. */
void ow_layout_close(void);

#endif
