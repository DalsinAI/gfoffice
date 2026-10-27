/*
 * owfpresent.library 3.2, OpenPresentation's part of libowf: presentations.
 * The deck model, its formats (ODP, flat ODP) and the page PDF writer, with
 * the document model its text uses.
 *
 * Its calls are owfpresent_lib.sfd's (owfpresent_calls.h, made from it by
 * scripts/gen-lib-calls.py); the rest is owf_libbase.h. The first 17 calls
 * are the same in all four of GFOffice's libraries
 * (docs/suite/owf-libraries.md).
 *
 * 3.1: owf_obj.font_scale and line_reduction (PowerPoint's normAutofit), so
 * owf_obj is larger, and owf_pdf_fallback, the PDF's stand-ins for
 * characters WinAnsiEncoding hasn't (OpenPresentation 0.2.1).
 *
 * 3.2: owf_doc.header_left and footer_left (owfwrite.library 3.2), so the
 * document model's owf_doc is larger and a program's size check refuses
 * 3.1. No call changes.
 *
 * Copyright (c) 2026 AmigaChrome Team. MIT.
 */
#define OWF_LIB_NAME "owfpresent.library"
#define OWF_LIB_VERSION 3
#define OWF_LIB_REVISION 2
#define OWF_LIB_VERS "3.2"
#define OWF_LIB_DATE "27.10.2026"
#define OWF_LIB_CALLS_H "owfpresent_calls.h"
#include "owf_libbase.h"
