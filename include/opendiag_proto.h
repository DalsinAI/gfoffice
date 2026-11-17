/*
 * The calls through owfdiag.library, for a program (or one file of it) built on it (OWF_USE_DIAG_LIBRARY;
 * docs/suite/owf-libraries.md): the library's inline calls (libowf/library/sdk, <proto/owfdiag.h>), which are
 * macros of the calls' names, so they come after every declaration. owf_diagram.h and the editing core's headers
 * (opendiag_core.h and the rest the library holds) include this at their end; the first to do so includes them all
 * here, and the calls come once every one of them is complete (each defines its _DONE first), so whichever a file
 * includes first, every call is declared before the macros. The library's first 17 calls
 * are the shared ones of GFOffice's libraries (memory, reports, kinds, zip), so a file on owfdiag.library uses no
 * other GFOffice library.
 *
 * odg_port_point returns a structure, which a library call can't: on the library it is odg_port_xy, here.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#if defined(OWF_USE_DIAG_LIBRARY) && !defined(OWF_LIBRARY_BUILD) && !defined(OPENDIAG_PROTO_H)

#if defined(OWF_USE_WRITE_LIBRARY) || defined(OWF_USE_CALC_LIBRARY) || defined(OWF_USE_PUB_LIBRARY) || \
    defined(OWF_USE_PRESENT_LIBRARY)
#error "opendiag_proto.h: one library's calls per file (OWF_USE_DIAG_LIBRARY and another)"
#endif

/* Not while one of the headers is still being read (its guard set, its _DONE not yet): it includes this again at its
 * own end. Then every header that declares the library's calls, and the calls once all are complete. */
#if !((defined(OPENDIAG_CORE_H) && !defined(OPENDIAG_CORE_H_DONE)) || \
    (defined(OPENDIAG_CLIP_H) && !defined(OPENDIAG_CLIP_H_DONE)) || \
    (defined(OPENDIAG_ORDER_H) && !defined(OPENDIAG_ORDER_H_DONE)) || \
    (defined(OPENDIAG_ALIGN_H) && !defined(OPENDIAG_ALIGN_H_DONE)) || \
    (defined(OPENDIAG_MIND_H) && !defined(OPENDIAG_MIND_H_DONE)) || \
    (defined(OPENDIAG_STYLE_H) && !defined(OPENDIAG_STYLE_H_DONE)) || \
    (defined(OPENDIAG_STENCIL_H) && !defined(OPENDIAG_STENCIL_H_DONE)) || \
    (defined(OPENDIAG_REXX_H) && !defined(OPENDIAG_REXX_H_DONE)) || \
    (defined(OWF_DIAGRAM_H) && !defined(OWF_DIAGRAM_H_DONE)))
#include "owf_diagram.h"
#include "opendiag_core.h"
#include "opendiag_clip.h"
#include "opendiag_order.h"
#include "opendiag_align.h"
#include "opendiag_mind.h"
#include "opendiag_style.h"
#include "opendiag_stencil.h"
#include "opendiag_rexx.h"

#if !defined(OPENDIAG_PROTO_H) && defined(OPENDIAG_CORE_H_DONE) && \
    defined(OPENDIAG_CLIP_H_DONE) && \
    defined(OPENDIAG_ORDER_H_DONE) && \
    defined(OPENDIAG_ALIGN_H_DONE) && \
    defined(OPENDIAG_MIND_H_DONE) && \
    defined(OPENDIAG_STYLE_H_DONE) && \
    defined(OPENDIAG_STENCIL_H_DONE) && \
    defined(OPENDIAG_REXX_H_DONE) && \
    defined(OWF_DIAGRAM_H_DONE)
#define OPENDIAG_PROTO_H

#include <proto/owfdiag.h>

#include <stdarg.h>
#include <stdio.h>

/* owf_report_add takes "...", which a library call can't: formatted here, as owf_proto.h does for the others */
static inline void odg_lib_report_add(owf_report *report, owf_note_level level, const char *fmt, ...)
{
    char text[256];
    va_list ap;
    if (!report)
        return;
    va_start(ap, fmt);
    vsnprintf(text, sizeof text, fmt, ap);
    va_end(ap);
    owf_report_text(report, level, text);
}
#define owf_report_add odg_lib_report_add

static inline owf_dpoint odg_port_point(const owf_dnum box[4], odg_port port)
{
    owf_dpoint at;
    odg_port_xy(box, port, &at);
    return at;
}

#endif

#endif

#endif
