/*
 * The calls through a library, for a program (or one file of it) built on
 * one of GFOffice's libraries (owf.h, docs/suite/owf-libraries.md): the
 * library's inline calls (libowf/library/sdk), which are macros of the
 * calls' names. owf.h, owf_calc.h, owf_pub.h, owf_zip.h, owf_deck.h and
 * owf_pdf.h each include this at their end; it acts once, when all of them
 * are complete, so every declaration comes before the macros.
 *
 * One library per file: the four share their first 17 calls' names
 * (memory, reports, kinds, zip), so a program that uses two or more, as
 * OWConvert does, keeps each one's calls in a file of their own.
 *
 * owf_report_add takes "...", which a library call can't: on a library it
 * is formatted here, with the program's own vsnprintf, and handed over as
 * text (owf_report_text). A note is cut at 255 bytes.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#if defined(OWF_USE_LIBRARY) && !defined(OWF_LIBRARY_BUILD) && !defined(OWF_PROTO_H) && defined(OWF_H_DONE) && \
    defined(OWF_CALC_H_DONE) && defined(OWF_PUB_H_DONE) && defined(OWF_ZIP_H_DONE) && defined(OWF_DECK_H_DONE) && \
    defined(OWF_PDF_H_DONE)
#define OWF_PROTO_H

#if defined(OWF_USE_WRITE_LIBRARY) + defined(OWF_USE_CALC_LIBRARY) + defined(OWF_USE_PUB_LIBRARY) + \
    defined(OWF_USE_PRESENT_LIBRARY) > 1
#error "owf_proto.h: one library's calls per file (OWF_USE_WRITE_LIBRARY, _CALC_, _PUB_ or _PRESENT_)"
#endif

#if defined(OWF_USE_WRITE_LIBRARY)
#include <proto/owfwrite.h>
#elif defined(OWF_USE_CALC_LIBRARY)
#include <proto/owfcalc.h>
#elif defined(OWF_USE_PUB_LIBRARY)
#include <proto/owfpub.h>
#elif defined(OWF_USE_PRESENT_LIBRARY)
#include <proto/owfpresent.h>
#else
/* owf.library 2, the one library before the split */
#include <proto/owf.h>
#endif

#if defined(OWF_USE_WRITE_LIBRARY) || defined(OWF_USE_CALC_LIBRARY) || defined(OWF_USE_PUB_LIBRARY) || \
    defined(OWF_USE_PRESENT_LIBRARY)
#include <stdarg.h>
#include <stdio.h>

static inline void owf_lib_report_add(owf_report *report, owf_note_level level, const char *fmt, ...)
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
#define owf_report_add owf_lib_report_add
#endif

#endif
