/*
 * Opening owf.library from a program (OWF_USE_LIBRARY), or nothing at all
 * when libowf is linked in. Include it in the one file that holds main():
 *
 *     if (!owf_open()) { ...owf.library 2 is not in LIBS:... }
 *     ...
 *     owf_close();
 *
 * owf.library 2 shares structures with the program: a document's
 * paragraphs and runs, a book's sheets and cells, which programs index
 * themselves. owf_open() opens version 2 or later and checks that the
 * library's structures are the sizes this program was built with
 * (owf_struct_size); when they are not, it closes the library again, sets
 * owf_open_sizes_differ and returns 0, so the program says so instead of
 * reading the wrong bytes. A program built for version 1 opens version 2
 * too: version 1's calls and structures are unchanged.
 *
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#ifndef OWF_OPEN_H
#define OWF_OPEN_H

#include "owf.h"

#define OWF_LIBRARY_NAME "owf.library"
#define OWF_LIBRARY_VERSION 2

#ifdef OWF_USE_LIBRARY

#include <proto/exec.h>

struct Library *OWFBase;
static int owf_open_sizes_differ;

static inline int owf_open(void)
{
    static const unsigned short sizes[OWF_STRUCT_COUNT] = { OWF_STRUCT_SIZES };
    int i;
    if (!OWFBase) {
        OWFBase = OpenLibrary((CONST_STRPTR)OWF_LIBRARY_NAME, OWF_LIBRARY_VERSION);
        for (i = 0; OWFBase && i < OWF_STRUCT_COUNT; i++) {
            if (owf_struct_size(i) != sizes[i]) {
                CloseLibrary(OWFBase);
                OWFBase = NULL;
                owf_open_sizes_differ = 1;
            }
        }
    }
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
