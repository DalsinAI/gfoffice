/* ogt_openfont: the Open apps' Amiga fonts through openfont.library, never
 * diskfont.library or bullet.library. openfont.library 1.2's
 * OF_OpenTextFont gives the TextFont diskfont's OpenDiskFont would for the
 * same TextAttr (bitmap fonts from FONTS:, scaled, and outline fonts drawn
 * by openfont itself); close it with graphics' CloseFont. The one call, by
 * its LVO in its register (openamigafont sdk/SFD/openfont_lib.sfd,
 * revision 2): no SDK headers needed.
 * MIT, Copyright (c) 2026 AmigaChrome Team. */
#ifndef OGT_OPENFONT_H
#define OGT_OPENFONT_H

#include <exec/libraries.h>
#include <graphics/text.h>
#include <proto/exec.h>

/* openfont.library 1.2 or later; NULL when there is none, or an older one,
 * or a CPU it refuses (it needs a 68040 with its FPU) */
static inline struct Library *ogt_openfont_open(void)
{
    struct Library *b = OpenLibrary((CONST_STRPTR)"openfont.library", 1);
    if (b && b->lib_Version == 1 && b->lib_Revision < 2) {
        CloseLibrary(b);
        b = NULL;
    }
    return b;
}

static inline struct TextFont *ogt_openfont_font(struct Library *b, struct TextAttr *ta)
{
    register ULONG d0_ __asm("d0");
    register struct Library *a6_ __asm("a6") = b;
    register struct TextAttr *a0_ __asm("a0") = ta;
    __asm volatile ("jsr -162(%%a6)" : "=r"(d0_), "+r"(a0_) : "r"(a6_) : "d1", "a1", "cc", "memory");
    return (struct TextFont *)d0_;
}

#endif
