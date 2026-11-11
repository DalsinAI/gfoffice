/*
 * libowf: footnotes and endnotes (owf.h's owf_note) for the readers and writers that keep them (ODT, DOCX),
 * the others that list them at the end, the layout and the editor. A note's mark is an empty text run that
 * names it (owf_run.note: 1 + its index).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef OWF_NOTE_H
#define OWF_NOTE_H

#include "owf_internal.h"

/* A new note: its index, -1 when memory runs out. text NULL: none; label NULL or "": numbered. */
int owf_note_add(owf_doc *doc, int endnote, const char *text, const char *label);
/* A copy of note index to change (owf_note says why): its index, -1 when memory runs out. */
int owf_note_copy(owf_doc *doc, int index);

/* A note's mark as a paragraph's run: mark is owf_run.note's value. 0, or -1 when memory runs out. */
int owf_para_add_note(owf_para *para, const owf_charfmt *fmt, int mark);
void owf_builder_note(owf_builder *b, int mark);

/* After a reader: marks naming no note go. */
void owf_note_finish(owf_doc *doc);

/* How many marks name notes of a kind (endnote 0 or 1; -1: both) in the document. */
int owf_doc_note_count(const owf_doc *doc, int endnote);

/* Note index's number among its kind, in the order the marks come (the body, then the other stories as
 * owf_doc_story has them), from 1; 0 when no mark names it. */
int owf_note_number(const owf_doc *doc, int index);

/* What note index's mark shows: its label, or its number (footnotes 1, 2; endnotes i, ii). "" when no mark
 * names it. */
void owf_note_mark_text(const owf_doc *doc, int index, char *out, size_t size);
/* A number in lower case roman (i, ii, iv), 1 to 3999; others in digits. */
void owf_note_roman(int number, char *out, size_t size);

/* Where note index's mark is: 1 and its story (owf_doc_story's n), paragraph and run, or 0. Any out pointer
 * may be NULL. */
int owf_note_place(const owf_doc *doc, int index, int *story, int *para, int *run);

/* The notes of a kind, as plain text for the formats that list them at the end: a line "mark<tab>text" each
 * (its paragraphs joined by spaces), in number order, after a line "Notes" (footnotes) or "Endnotes".
 * Appended to out; 0, or -1 when memory runs out. Nothing when there are none. */
int owf_notes_listing(const owf_doc *doc, int endnote, owf_buf *out);

#endif
