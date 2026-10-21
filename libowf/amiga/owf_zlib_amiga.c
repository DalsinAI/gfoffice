/*
 * z.library for libowf on the Amiga: opened the first time a document needs
 * deflate or inflate, closed by owf_z_close() (owf.library's expunge, or a
 * program's exit when libowf is linked in).
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "owf_zlib.h"

#include <proto/exec.h>

/* openzlib's z.library is 53.12; any 53 has the calls libowf uses */
#define OWF_Z_VERSION 53

struct Library *ZBase;

int owf_z_ready(void)
{
    if (!ZBase)
        ZBase = OpenLibrary((CONST_STRPTR)"z.library", OWF_Z_VERSION);
    return ZBase != NULL;
}

void owf_z_close(void)
{
    if (ZBase) {
        CloseLibrary(ZBase);
        ZBase = NULL;
    }
}

#ifndef OWF_LIBRARY_BUILD
/* a program with libowf linked in closes it as it ends */
static void __attribute__((destructor)) owf_z_exit(void)
{
    owf_z_close();
}
#endif
