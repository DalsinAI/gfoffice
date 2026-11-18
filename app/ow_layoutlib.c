/* ow_layoutlib.h: gfopenlayout.library opened for a GFOffice program, with the version it needs, before the
 * program has a window. The program has this OpenLayoutBase of its own, so GFOpenLayout's link sources
 * (gfopenlayout_link.c, gfopenlayout_link_core.c) take it and their own start-up code is not linked: that
 * one opens version 10 and says what is wrong only to a Shell.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include "ow_layoutlib.h"

#ifdef OW_LAYOUT_LIBRARY
#include <exec/types.h>
#include <exec/libraries.h>
#include <proto/exec.h>

struct Library *OpenLayoutBase;

int ow_layout_open(void)
{
    if (!OpenLayoutBase)
        OpenLayoutBase = OpenLibrary((CONST_STRPTR)"gfopenlayout.library", OW_LAYOUT_LIBRARY_VERSION);
    return OpenLayoutBase != NULL;
}

void ow_layout_close(void)
{
    if (OpenLayoutBase) CloseLibrary(OpenLayoutBase);
    OpenLayoutBase = NULL;
}

int ow_layout_is_library(void) { return 1; }

#else

int ow_layout_open(void) { return 1; }
void ow_layout_close(void) { }
int ow_layout_is_library(void) { return 0; }

#endif
