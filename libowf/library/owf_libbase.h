/*
 * What each of GFOffice's four libraries is besides its calls
 * (docs/suite/owf-libraries.md): its first code, its vector table, its
 * resident tag, and opening, closing and expunging. Each library is one
 * small file (owfwrite_library.c and the others) that names itself and
 * includes this:
 *
 *     #define OWF_LIB_NAME "owfwrite.library"
 *     #define OWF_LIB_VERSION 3
 *     #define OWF_LIB_REVISION 0
 *     #define OWF_LIB_VERS "3.0"
 *     #define OWF_LIB_DATE "25.10.2026"
 *     #define OWF_LIB_CALLS_H "owfwrite_calls.h"
 *     #include "owf_libbase.h"
 *
 * OWF_LIB_CALLS_H is the library's calls (scripts/gen-lib-calls.py makes
 * it from the library's SFD). The rest of the library is libowf, linked
 * from build-library.sh's archive: only what the calls reach.
 *
 * Global state, as owf.library's: SysBase, DOSBase, the runtime's memory
 * pool (one per library: memory from one library goes back to the same
 * library's owf_free) and z.library's base, opened the first time a
 * document needs inflate or deflate. A document, book, deck or .pub
 * belongs to the task using it; libowf keeps no state between calls.
 *
 * Copyright (c) 2026 AmigaChrome Team. MIT.
 */
#ifndef OWF_LIBBASE_H
#define OWF_LIBBASE_H

#include <exec/types.h>
#include <exec/resident.h>
#include <exec/libraries.h>
#include <exec/execbase.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <proto/exec.h>

#include "owf.h"
#include "owf_calc.h"
#include "owf_pub.h"
#include "owf_zip.h"
#include "owf_deck.h"
#include "owf_pdf.h"
#include "owf_runtime.h"
#include "owf_zlib.h"

#if !defined(OWF_LIB_NAME) || !defined(OWF_LIB_VERSION) || !defined(OWF_LIB_REVISION) || !defined(OWF_LIB_VERS) || \
    !defined(OWF_LIB_DATE) || !defined(OWF_LIB_CALLS_H)
#error "owf_libbase.h: name the library first (OWF_LIB_NAME ... OWF_LIB_CALLS_H)"
#endif

#define REG(r, decl) register decl __asm(#r)

struct OWFLibBase {
    struct Library lib;
    BPTR seglist;
};

struct ExecBase *SysBase;
struct DosLibrary *DOSBase;

/* Run from the Shell by mistake, the library does nothing. */
int start(void) { return -1; }

static const char lib_name[] = OWF_LIB_NAME;
static const char lib_id[] =
    OWF_LIB_NAME " " OWF_LIB_VERS " (" OWF_LIB_DATE ") libowf " OWF_VERSION ", AmigaChrome Team\r\n";
static const char lib_ver[] __attribute__((used)) =
    "$VER: " OWF_LIB_NAME " " OWF_LIB_VERS " (" OWF_LIB_DATE ") MIT, Copyright (c) 2026 AmigaChrome Team";

static struct Library *lib_init(REG(d0, struct OWFLibBase *base), REG(a0, BPTR seglist),
                                REG(a6, struct ExecBase *sys));
static struct Library *lib_open(REG(a6, struct OWFLibBase *base));
static BPTR lib_close(REG(a6, struct OWFLibBase *base));
static BPTR lib_expunge(REG(a6, struct OWFLibBase *base));
static ULONG lib_null(void);

#include OWF_LIB_CALLS_H

static const APTR lib_vectors[] = {
    (APTR)lib_open, (APTR)lib_close, (APTR)lib_expunge, (APTR)lib_null,
    OWF_LIB_CALLS,
    (APTR)-1
};

static const struct {
    ULONG size;
    const APTR *vectors;
    APTR data;
    APTR init;
} lib_inittable = {
    sizeof(struct OWFLibBase), lib_vectors, NULL, (APTR)lib_init
};

const struct Resident lib_romtag = {
    RTC_MATCHWORD, (struct Resident *)&lib_romtag, (APTR)(&lib_romtag + 1),
    RTF_AUTOINIT, OWF_LIB_VERSION, NT_LIBRARY, 0,
    (char *)lib_name, (char *)lib_id, (APTR)&lib_inittable
};

static struct Library *lib_init(REG(d0, struct OWFLibBase *base), REG(a0, BPTR seglist),
                                REG(a6, struct ExecBase *sys))
{
    SysBase = sys;
    base->seglist = seglist;
    base->lib.lib_Revision = OWF_LIB_REVISION;
    DOSBase = (struct DosLibrary *)OpenLibrary((CONST_STRPTR)"dos.library", 37);
    if (!DOSBase || !owf_rt_init()) {
        owf_rt_cleanup();
        if (DOSBase) CloseLibrary((struct Library *)DOSBase);
        FreeMem((UBYTE *)base - base->lib.lib_NegSize,
                base->lib.lib_NegSize + base->lib.lib_PosSize);
        return NULL;
    }
    return &base->lib;
}

static struct Library *lib_open(REG(a6, struct OWFLibBase *base))
{
    base->lib.lib_OpenCnt++;
    base->lib.lib_Flags &= ~LIBF_DELEXP;
    return &base->lib;
}

static BPTR lib_close(REG(a6, struct OWFLibBase *base))
{
    base->lib.lib_OpenCnt--;
    if (base->lib.lib_OpenCnt == 0 && (base->lib.lib_Flags & LIBF_DELEXP))
        return lib_expunge(base);
    return 0;
}

static BPTR lib_expunge(REG(a6, struct OWFLibBase *base))
{
    BPTR seglist;
    if (base->lib.lib_OpenCnt) {
        base->lib.lib_Flags |= LIBF_DELEXP;
        return 0;
    }
    seglist = base->seglist;
    Remove(&base->lib.lib_Node);
    owf_z_close();
    owf_rt_cleanup();
    CloseLibrary((struct Library *)DOSBase);
    FreeMem((UBYTE *)base - base->lib.lib_NegSize,
            base->lib.lib_NegSize + base->lib.lib_PosSize);
    return seglist;
}

static ULONG lib_null(void) { return 0; }

#endif
