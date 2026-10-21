/*
 * Opening owf.library from a program (OWF_USE_LIBRARY), or nothing at all
 * when libowf is linked in. Include it in the one file that holds main():
 *
 *     if (!owf_open()) { ...owf.library 1 is not in LIBS:... }
 *     ...
 *     owf_close();
 *
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#ifndef OWF_OPEN_H
#define OWF_OPEN_H

#include "owf.h"

#define OWF_LIBRARY_NAME "owf.library"
#define OWF_LIBRARY_VERSION 1

#ifdef OWF_USE_LIBRARY

#include <proto/exec.h>

struct Library *OWFBase;

static inline int owf_open(void)
{
    if (!OWFBase)
        OWFBase = OpenLibrary((CONST_STRPTR)OWF_LIBRARY_NAME, OWF_LIBRARY_VERSION);
    return OWFBase != NULL;
}

static inline void owf_close(void)
{
    if (OWFBase) {
        CloseLibrary(OWFBase);
        OWFBase = NULL;
    }
}

#else

static inline int owf_open(void) { return 1; }
static inline void owf_close(void) { }

#endif

#endif
