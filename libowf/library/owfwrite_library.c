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
 * 3.7: libowf 1.8, footnotes and endnotes (owf_run.note, owf_doc.notes, owf_note): ODT text:note and DOCX
 * footnotes.xml and endnotes.xml keep them, RTF its own \footnote, HTML, plain text and PDF list them after
 * the text. No new calls; owf_run and owf_doc grew, so the three libraries go together again.
 * 3.8: libowf 1.9, mail merge fields (OWF_FIELD_MERGE) and the data source (owf_doc.merge_source): DOCX
 * MERGEFIELD and w:mailMerge, ODT text:database-display; other formats the value shown. No new calls; owf_doc
 * grew, so the three libraries go together again.
 * 3.9: libowf 1.10, alphabetical index entries (OWF_FIELD_INDEX): DOCX XE fields and ODT
 * text:alphabetical-index-mark; other formats show nothing for them. No new calls and no struct grew.
 * 3.10: libowf 1.11, tracked format changes of runs (owf_run.format_change, owf_doc.format_changes,
 * owf_format_change): DOCX w:rPrChange in and out; other formats the format as it is now (accepted). No new
 * calls; owf_run and owf_doc grew, so the three libraries go together again.
 * 3.11: libowf 1.12, a picture in the line may be a tracked change (owf_run.revision on OWF_RUN_IMAGE): DOCX
 * w:ins and w:del round its w:drawing, ODT changed regions, in and out. A frame's run may be one too (DOCX in
 * and out; ODT saves it accepted). No new calls and no struct grew.
 * 3.12: libowf 1.12.1 (after 3.11's 1.12.0), footnote and endnote numbering (owf_page.note_format and
 * note_start): DOCX w:footnotePr and w:endnotePr, ODT text:notes-configuration, in and out; the marks in every
 * format numbered by them. No new calls; owf_page grew, and owf_doc with it, so the three libraries go together
 * again.
 * 3.13: libowf 1.12.2, the notes' text with its formats (owf_doc.note_stories, OWF_DOC_NOTE_STORY): DOCX's
 * notes' runs in and out. No new calls; owf_doc grew, so the three libraries go together again.
 * 3.14: libowf 1.12.3, notes numbered again at each section or page (owf_page.note_restart): DOCX
 * w:numRestart, ODT text:start-numbering-at, in and out. No new calls; owf_page grew, and owf_doc with it.
 * 3.15: libowf 1.12.4, number ranges (OWF_FIELD_SEQ): DOCX SEQ fields, ODT text:sequence and its
 * text:sequence-decl, in and out. No new calls and no structure grew, so owfpub and owfpresent stay.
 * 3.16: libowf 1.12.5 (3.14 and 3.15, libowf 1.12.3 and 1.12.4, are #288's and #292's), a table row may be a
 * tracked change (owf_para.row_revision: DOCX w:trPr/w:ins and w:del; ODT saves it accepted). No new calls;
 * owf_para grew, so the three libraries go together again.
 * 3.17: libowf 1.12.6, 3.15's number ranges (OWF_FIELD_SEQ) on top of 3.16: #292 was numbered 3.15 before
 * #255's 3.16 merged first, so the library that knows SEQ is 3.17. No new calls and no structure grew.
 * 3.19: libowf 1.14 (3.18, libowf 1.13, is #368's), hidden text (OWF_HIDDEN): DOCX w:vanish, ODT
 * text:display="none" and RTF \v in and out; PDF and Markdown leave it out. No new calls and no structure
 * grew, so owfpub and owfpresent stay.
 * 3.21: libowf 1.16 (3.18 to 3.20, libowf 1.13 to 1.15, are #368's, #373's and #375's), conditional text
 * (OWF_FIELD_IF, owf_ifform.h): DOCX IF with its fields nested in its code, and ODT text:conditional-text,
 * in and out. No new calls and no structure grew, so owfpub and owfpresent stay.
 * 3.22: libowf 1.19 (numbered 3.18, libowf 1.13, until 3.19 to 3.21 and libowf 1.14 to 1.18 merged first), tracked paragraph format
 * changes (owf_para.para_format_change, owf_doc.para_format_changes, owf_para_format_change): DOCX w:pPrChange
 * in and out; other formats the format as it is now (accepted). No new calls; owf_para and owf_doc grew, so the
 * three libraries go together again (owfpub.library 3.7, owfpresent.library 3.17).
 *
 * Copyright (c) 2026 AmiCradle Team. MIT.
 */
#define OWF_LIB_NAME "owfwrite.library"
#define OWF_LIB_VERSION 3
#define OWF_LIB_REVISION 22
#define OWF_LIB_VERS "3.22"
#define OWF_LIB_DATE "16.11.2026"
#define OWF_LIB_CALLS_H "owfwrite_calls.h"
#include "owf_libbase.h"
