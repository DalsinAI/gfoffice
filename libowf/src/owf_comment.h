/*
 * libowf: comments (owf.h's owf_annotation) for the readers and writers that keep them (ODT, DOCX) and the editor.
 * A comment's place is an empty text run that names it (owf_run.comment): 1 + its index where it is anchored,
 * -(1 + its index) where its range starts.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef OWF_COMMENT_H
#define OWF_COMMENT_H

#include "owf_internal.h"

/* A new comment: its index, -1 when memory runs out. Any string may be NULL (text: none). parent -1: none. */
int owf_comment_add(owf_doc *doc, const char *author, const char *initials, const char *date, const char *text,
                    int parent);
/* A copy of comment index to change (owf_annotation says why): its index, -1 when memory runs out. */
int owf_comment_copy(owf_doc *doc, int index);

/* A comment's place as a paragraph's run: mark is owf_run.comment's value. 0 or -1 when memory runs out. */
int owf_para_add_comment(owf_para *para, const owf_charfmt *fmt, int mark);
void owf_builder_comment(owf_builder *b, int mark);

/* After a reader: a range's start with no anchor becomes the anchor (a comment on a point), and marks naming
 * no comment go. */
void owf_comment_finish(owf_doc *doc);

/* Whether a document has a comment in it (a run anchoring one). */
int owf_doc_has_comments(const owf_doc *doc);

/* Where comment index is anchored: 1 and its story (owf_doc_story's n), paragraph and run, or 0 (not in the
 * document). Any out pointer may be NULL. */
int owf_comment_place(const owf_doc *doc, int index, int *story, int *para, int *run);

/* Every story a comment can be in, in order: the body, the headers and footers, the text boxes. NULL after the
 * last. */
owf_story *owf_doc_story(owf_doc *doc, int n);

#endif
