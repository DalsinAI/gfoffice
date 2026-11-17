/*
 * odg_sizes.c: the sizes of the structures a program on owfdiag.library shares with it (opendiag_core.h,
 * OWF_DSTRUCT_), for owf_struct_size to answer and owf_open() (owf_open.h) to check.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "opendiag_core.h"
#include "opendiag_clip.h"
#include "opendiag_stencil.h"
#include "opendiag_rexx.h"

/* the diagram numbers start past owf.h's own, which owf_struct_size answers first */
typedef char odg_sizes_past_owf[(int)OWF_STRUCT_COUNT <= (int)OWF_DSTRUCT_FIRST ? 1 : -1];

unsigned long odg_struct_size(int which)
{
    static const struct { unsigned short which, size; } sizes[] = { ODG_STRUCT_SIZES };
    int i;
    for (i = 0; i < (int)(sizeof sizes / sizeof sizes[0]); i++)
        if (sizes[i].which == which)
            return sizes[i].size;
    return 0;
}
