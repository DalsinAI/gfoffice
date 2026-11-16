/*
 * GFOpenPublisher's mail merge on the shared page model (src/pub/opp_merge.c).
 * The data source is the suite's merge table (include/ow_merge.h), the same
 * one GFOpenWrite reads. A merge field is a field run (OWF_RUN_FIELD,
 * OWF_FIELD_MERGE) in a frame's text: its href is the column, its text the
 * value shown. MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef OPENPUBLISHER_MERGE_H
#define OPENPUBLISHER_MERGE_H

#include "owf_deck.h"
#include "ow_merge.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OPP_MERGE_VERSION "0.1.0"

/* A merge field for column put at the end of paragraph para of an object's
 * text (para -1: the last; a paragraph is added when there is none). It
 * shows the column's name in guillemets until a record is shown.
 * OWF_OK, OWF_ERR_FORMAT (no object, no column, no such paragraph) or
 * OWF_ERR_MEMORY. */
int opp_merge_insert_field(owf_obj *obj, int para, const char *column, const owf_charfmt *fmt);

/* How many merge fields the pages and masters have; *missing (when not
 * NULL): how many of them name a column the table does not have (a NULL
 * table has none). */
int opp_merge_fields(const owf_deck *deck, const ow_merge_table *t, int *missing);

/* Every merge field on the pages and masters shows record (from 0) of the
 * table; -1 (or a NULL table) shows each field's column name in guillemets,
 * as does a field whose column the table does not have. *set (when not
 * NULL): how many fields were set. OWF_OK; OWF_ERR_FORMAT: no deck;
 * OWF_ERR_UNSUPPORTED: a record the table does not have; OWF_ERR_MEMORY
 * (fields already set keep their new text). */
int opp_merge_show(owf_deck *deck, const ow_merge_table *t, int record, int *set);

/* Merge to a new publication, in place: the deck's pages come once for each
 * record first..last (from 0; first -1: the first, last -1: the last), in
 * record order, with each merge field made plain text of its value (a line
 * break in a value a line break). Each record's copies of a text flow are a
 * flow of their own. Masters are left as they are. OWF_OK;
 * OWF_ERR_UNSUPPORTED: no table, no records or no pages, or a range the
 * table does not have; OWF_ERR_MEMORY: the deck is as it was. */
int opp_merge_pages(owf_deck *deck, const ow_merge_table *t, int first, int last);

#ifdef __cplusplus
}
#endif
#endif
