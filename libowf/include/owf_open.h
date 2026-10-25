/*
 * Opening a program's library (OWF_USE_WRITE_LIBRARY, OWF_USE_CALC_LIBRARY,
 * OWF_USE_PUB_LIBRARY or OWF_USE_PRESENT_LIBRARY; owf.h), or nothing at all
 * when libowf is linked in. Include it in one file per library: the one
 * that holds main(), or, in a program that uses several libraries
 * (OWConvert), each library's own file.
 *
 *     if (!owf_open()) { ...OWF_LIBRARY_NAME OWF_LIBRARY_VERSION is not in LIBS:... }
 *     ...
 *     owf_close();
 *
 * The libraries share structures with the program: a document's
 * paragraphs and runs, a book's sheets and cells, a deck's slides, which
 * programs index themselves. owf_open() opens the library and checks that
 * the structures that library shares are the sizes this program was built
 * with (owf_struct_size, owf.h); when they are not, it closes the library
 * again, sets owf_open_sizes_differ and returns 0, so the program says so
 * instead of reading the wrong bytes. Only that library's structures are
 * checked, so a change to a workbook's structures doesn't stop OpenWrite.
 *
 * OWF_USE_LIBRARY alone is owf.library 2, the one library before the
 * split (docs/suite/owf-libraries.md), until OpenUp ships the four; it
 * checks every structure.
 *
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#ifndef OWF_OPEN_H
#define OWF_OPEN_H

#include "owf.h"
#include "owf_pub.h"
#include "owf_zip.h"
#include "owf_deck.h"

/* the structures of the document model, which owfwrite.library, owfpub.library and owfpresent.library share */
#define OWF_OPEN_DOC_SIZES                                                                                 \
    { OWF_STRUCT_CHARFMT, sizeof(owf_charfmt) }, { OWF_STRUCT_RUN, sizeof(owf_run) },                     \
    { OWF_STRUCT_TAB, sizeof(owf_tab) }, { OWF_STRUCT_PARAFMT, sizeof(owf_parafmt) },                     \
    { OWF_STRUCT_PARA, sizeof(owf_para) }, { OWF_STRUCT_STORY, sizeof(owf_story) },                       \
    { OWF_STRUCT_FONT, sizeof(owf_font) }, { OWF_STRUCT_PAGE, sizeof(owf_page) },                         \
    { OWF_STRUCT_IMAGE, sizeof(owf_image) }, { OWF_STRUCT_FRAME, sizeof(owf_frame) },                     \
    { OWF_STRUCT_DOC, sizeof(owf_doc) }, { OWF_STRUCT_UNZIP, sizeof(owf_unzip) }

#if defined(OWF_USE_WRITE_LIBRARY)
#define OWF_LIBRARY_NAME "owfwrite.library"
#define OWF_LIBRARY_VERSION 3
#define OWF_LIBRARY_BASE OWFWriteBase
#define OWF_OPEN_SIZES OWF_OPEN_DOC_SIZES, { OWF_STRUCT_FORMAT, sizeof(owf_format) }
#elif defined(OWF_USE_CALC_LIBRARY)
#define OWF_LIBRARY_NAME "owfcalc.library"
#define OWF_LIBRARY_VERSION 3
#define OWF_LIBRARY_BASE OWFCalcBase
#define OWF_OPEN_SIZES                                                                                     \
    { OWF_STRUCT_CHARFMT, sizeof(owf_charfmt) }, { OWF_STRUCT_CELL, sizeof(owf_cell) },                   \
    { OWF_STRUCT_CELLSTYLE, sizeof(owf_cellstyle) }, { OWF_STRUCT_COLINFO, sizeof(owf_colinfo) },         \
    { OWF_STRUCT_ROWINFO, sizeof(owf_rowinfo) }, { OWF_STRUCT_RANGE, sizeof(owf_range) },                 \
    { OWF_STRUCT_SHEET, sizeof(owf_sheet) }, { OWF_STRUCT_DEFNAME, sizeof(owf_defname) },                 \
    { OWF_STRUCT_BOOK, sizeof(owf_book) }, { OWF_STRUCT_BOOK_FORMAT, sizeof(owf_book_format) },           \
    { OWF_STRUCT_CONDFMT, sizeof(owf_condfmt) }, { OWF_STRUCT_COMMENT, sizeof(owf_comment) },             \
    { OWF_STRUCT_VALIDATION, sizeof(owf_validation) }, { OWF_STRUCT_PART, sizeof(owf_part) },             \
    { OWF_STRUCT_CHART_POINT, sizeof(owf_chart_point) }, { OWF_STRUCT_CHART_SERIES, sizeof(owf_chart_series) }, \
    { OWF_STRUCT_CHART, sizeof(owf_chart) }, { OWF_STRUCT_UNZIP, sizeof(owf_unzip) }
#elif defined(OWF_USE_PUB_LIBRARY)
#define OWF_LIBRARY_NAME "owfpub.library"
#define OWF_LIBRARY_VERSION 3
#define OWF_LIBRARY_BASE OWFPubBase
#define OWF_OPEN_SIZES                                                                                     \
    OWF_OPEN_DOC_SIZES, { OWF_STRUCT_PUB, sizeof(owf_pub) }, { OWF_STRUCT_PUB_PAGE, sizeof(owf_pub_page) }, \
    { OWF_STRUCT_PUB_FRAME, sizeof(owf_pub_frame) }
#elif defined(OWF_USE_PRESENT_LIBRARY)
#define OWF_LIBRARY_NAME "owfpresent.library"
#define OWF_LIBRARY_VERSION 3
#define OWF_LIBRARY_BASE OWFPresentBase
#define OWF_OPEN_SIZES                                                                                     \
    OWF_OPEN_DOC_SIZES, { OWF_STRUCT_LOOK, sizeof(owf_look) }, { OWF_STRUCT_TEXTLEVEL, sizeof(owf_textlevel) }, \
    { OWF_STRUCT_OBJ, sizeof(owf_obj) }, { OWF_STRUCT_TRANSITION, sizeof(owf_transition) },               \
    { OWF_STRUCT_SLIDE, sizeof(owf_slide) }, { OWF_STRUCT_MASTER, sizeof(owf_master) },                   \
    { OWF_STRUCT_SLIDE_LAYOUT, sizeof(owf_slide_layout) }, { OWF_STRUCT_KEPT, sizeof(owf_kept) },         \
    { OWF_STRUCT_DECK_PART, sizeof(owf_deck_part) }, { OWF_STRUCT_DECK, sizeof(owf_deck) },               \
    { OWF_STRUCT_DECK_FORMAT, sizeof(owf_deck_format) }
#else
#define OWF_LIBRARY_NAME "owf.library"
#define OWF_LIBRARY_VERSION 2
#define OWF_LIBRARY_BASE OWFBase
#endif

#ifdef OWF_USE_LIBRARY

#include <proto/exec.h>

struct Library *OWF_LIBRARY_BASE;
static int owf_open_sizes_differ;

static inline int owf_open(void)
{
#ifdef OWF_OPEN_SIZES
    static const struct { unsigned short which, size; } sizes[] = { OWF_OPEN_SIZES };
    const int count = (int)(sizeof sizes / sizeof sizes[0]);
#define OWF_OPEN_WHICH(i) sizes[i].which
#define OWF_OPEN_SIZE(i) sizes[i].size
#else
    static const unsigned short sizes[OWF_STRUCT_COUNT] = { OWF_STRUCT_SIZES };
    const int count = OWF_STRUCT_COUNT;
#define OWF_OPEN_WHICH(i) (i)
#define OWF_OPEN_SIZE(i) sizes[i]
#endif
    int i;
    if (!OWF_LIBRARY_BASE) {
        OWF_LIBRARY_BASE = OpenLibrary((CONST_STRPTR)OWF_LIBRARY_NAME, OWF_LIBRARY_VERSION);
        for (i = 0; OWF_LIBRARY_BASE && i < count; i++) {
            if (owf_struct_size(OWF_OPEN_WHICH(i)) != OWF_OPEN_SIZE(i)) {
                CloseLibrary(OWF_LIBRARY_BASE);
                OWF_LIBRARY_BASE = NULL;
                owf_open_sizes_differ = 1;
            }
        }
    }
#undef OWF_OPEN_WHICH
#undef OWF_OPEN_SIZE
    return OWF_LIBRARY_BASE != NULL;
}

static inline void owf_close(void)
{
    if (OWF_LIBRARY_BASE) {
        CloseLibrary(OWF_LIBRARY_BASE);
        OWF_LIBRARY_BASE = NULL;
    }
}

#else

static inline int owf_open(void) { return 1; }
static inline void owf_close(void) { }

#endif

#endif
