/*
 * owfwrite.library 3.3, GFOpenWrite's part of libowf: documents. The document
 * model and its formats: DOCX, ODT, the Amiga word processors' files (Final
 * Writer, Wordworth, ProWrite, IFF FTXT), text, ANSI and HTML, and PDF out.
 *
 * Its calls are owfwrite_lib.sfd's (owfwrite_calls.h, made from it by
 * scripts/gen-lib-calls.py); the rest is owf_libbase.h. The first 17 calls
 * are the same in all four of GFOffice's libraries
 * (docs/suite/owf-libraries.md).
 *
 * 3.1: owf_frame.in_line (a byte that was reserved), set by the DOCX reader for
 * Word's text box in the line (GFOpenWrite 2.0.17); the structure's size is the
 * same.
 *
 * 3.2: owf_doc.header_left and footer_left, left pages' own header and
 * footer (ProWrite's, Word's even ones, ODF's style:header-left; GFOpenWrite
 * 2.0.23), at its end, so owf_doc is larger and a program's size check
 * refuses 3.1. No call changes.
 *
 * 3.3: owf_page_number(), the shared page-label service used by OpenLayout,
 * generated references and GFOpenWrite 2.0.26's Table of Contents.
 * 3.4: libowf 1.5, tracked changes (owf_run.revision, owf_para.mark_revision, owf_doc.revisions): ODT and
 * DOCX keep them, every other format is written with them accepted. No new calls; the documents' structs
 * grew, so owf_open's size check wants this owfwrite, owfpub and owfpresent together.
 * 3.5: libowf 1.6, comments (owf_run.comment, owf_doc.comments, owf_annotation): ODT office:annotation and
 * DOCX comments.xml with replies and done (commentsExtended); other formats leave them out and say so. No new
 * calls; the structs grew again, so the three libraries go together as for 3.4.
 * 3.6: libowf 1.7, fields with a value (title, author, subject, file name, word count, chapter, user fields,
 * references to a bookmark; owf_doc.author, .subject and .userfields): ODT and DOCX keep them, other formats
 * the value shown. No new calls; owf_doc grew, so the three libraries go together again.
 *
 * Copyright (c) 2026 AmiCradle Team. MIT.
 */
#define OWF_LIB_NAME "owfwrite.library"
#define OWF_LIB_VERSION 3
#define OWF_LIB_REVISION 6
#define OWF_LIB_VERS "3.6"
#define OWF_LIB_DATE "11.11.2026"
#define OWF_LIB_CALLS_H "owfwrite_calls.h"
#include "owf_libbase.h"
