/*
 * owfpub.library 3.5, GFOpenPressmaster's part of libowf: Publisher files. The
 * .pub reader and its compound-file reader, with the document model its text
 * uses.
 *
 * Its calls are owfpub_lib.sfd's (owfpub_calls.h, made from it by
 * scripts/gen-lib-calls.py); the rest is owf_libbase.h. The first 17 calls
 * are the same in all four of GFOffice's libraries
 * (docs/suite/owf-libraries.md).
 *
 * 3.1: owf_doc.header_left and footer_left (owfwrite.library 3.2), so the
 * document model's owf_doc is larger and a program's size check refuses
 * 3.0. No call changes.
 *
 * 3.2: owf_pub_frame's look (filled, lined, fill, line, line_width,
 * inset) and owf_pub's colour scheme, so owf_pub_frame and owf_pub are
 * larger and a program's size check refuses 3.1. No call changes.
 *
 * 3.3: owf_page.note_format and note_start (owfwrite.library 3.12, libowf 1.12.1), so owf_page and owf_doc
 * are larger and a program's size check refuses 3.2. No call changes.
 *
 * 3.4: owf_doc.note_stories (owfwrite.library 3.13, libowf 1.12.2), so owf_doc is larger and a program's size
 * check refuses 3.3. No call changes.
 * 3.5: owf_page.note_restart (owfwrite.library 3.14, libowf 1.12.3), so owf_page and owf_doc are larger and a
 * program's size check refuses 3.4. No call changes.
 * 3.6: owf_para.row_revision (owfwrite.library 3.16, libowf 1.12.5; 3.5 is #288's), so owf_para is larger
 * and a program's size check refuses 3.5 and before. No call changes.
 * 3.7: owf_para.para_format_change and owf_doc.para_format_changes (owfwrite.library 3.24, libowf 1.21), so
 * owf_para and owf_doc are larger and a program's size check refuses 3.6 and before. No call changes.
 *
 * Copyright (c) 2026 AmiCradle Team. MIT.
 */
#define OWF_LIB_NAME "owfpub.library"
#define OWF_LIB_VERSION 3
#define OWF_LIB_REVISION 7
#define OWF_LIB_VERS "3.7"
#define OWF_LIB_DATE "16.11.2026"
#define OWF_LIB_CALLS_H "owfpub_calls.h"
#include "owf_libbase.h"
