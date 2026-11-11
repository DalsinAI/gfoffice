/*
 * libowf: tracked changes (owf.h's owf_revision) for the readers and writers that keep them (ODT, DOCX) and
 * for the formats that don't, which are written with every change accepted.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef OWF_TRACK_H
#define OWF_TRACK_H

#include "owf_internal.h"

/* A change for the document's list: its number (owf_run.revision), 0 when memory runs out. author and date
 * may be NULL. The same author, date and kind twice give the same number. */
int owf_track_add(owf_doc *doc, int kind, const char *author, const char *date);

/* What a reader is putting into a builder: from where (story, paragraph and run) the runs it adds are
 * a change's (rev; 0: none). Begin it with the builder, switch it where the change changes; switching
 * marks the runs added since with the last change. With marks, the paragraphs ended between also have
 * their ends marked (ODT's insertions that run across paragraphs). */
typedef struct {
    owf_story *story;
    int para, run;
    int rev, marks;
} owf_track_span;
void owf_track_begin(owf_track_span *t, owf_builder *b);
void owf_track_switch(owf_track_span *t, owf_builder *b, int rev, int marks);

/* Splits a story's paragraph before its run `run`: the first part's end is change rev (a deleted paragraph
 * mark that a reader found in the middle of one). The second part has the old end, the format and the
 * bookmarks after the place. 0 when memory runs out. */
int owf_track_split_para(owf_story *story, int para, int run, int rev);

/* The builder's paragraph, made now if it has none open (a deleted paragraph mark at a paragraph's start).
 * NULL when memory runs out. (owf_doc.c, beside the builder.) */
owf_para *owf_builder_para(owf_builder *b);

/* After a reader: changes it began but didn't number (-1) and numbers the document hasn't go. */
void owf_track_finish(owf_doc *doc);

/* Whether a document has a change anywhere (a run's or a paragraph mark's). */
int owf_doc_has_changes(const owf_doc *doc);

/* The document as it is with every change accepted: inserted text kept, deleted text gone, paragraphs
 * whose ends were deleted joined. *view shares doc's text, fonts and pictures; free it with
 * owf_doc_accepted_free only (doc must not change meanwhile). OWF_OK or OWF_ERR_MEMORY. */
int owf_doc_accepted(const owf_doc *doc, owf_doc **view);
void owf_doc_accepted_free(owf_doc *view);

#endif
