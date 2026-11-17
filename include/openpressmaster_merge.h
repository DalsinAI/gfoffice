/*
 * GFOpenPressmaster's mail merge on the shared page model (src/pub/opp_merge.c).
 * The data source is the suite's merge table (include/ow_merge.h), the same
 * one GFOpenWrite reads. A merge field is a field run (OWF_RUN_FIELD,
 * OWF_FIELD_MERGE) in a frame's text: its href is the column, its text the
 * value shown. MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef OPENPRESSMASTER_MERGE_H
#define OPENPRESSMASTER_MERGE_H

#include "owf_deck.h"
#include "ow_merge.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OPP_MERGE_VERSION "0.3.0"

/* A merge field for column put at the end of paragraph para of an object's
 * text (para -1: the last; a paragraph is added when there is none). It
 * shows the column's name in guillemets until a record is shown.
 * OWF_OK, OWF_ERR_FORMAT (no object, no column, no such paragraph) or
 * OWF_ERR_MEMORY. */
int opp_merge_insert_field(owf_obj *obj, int para, const char *column, const owf_charfmt *fmt);

/* A merge field for column put at a place in an object's text: paragraph para,
 * offset bytes into it as GFOpenPresentation's editor counts them (opr_pos:
 * text bytes, a tab or a line break 1, a field 0). The field takes the look
 * of the text before it and goes after any field already there. OWF_OK,
 * OWF_ERR_FORMAT (no object, no column, no such place) or OWF_ERR_MEMORY. */
int opp_merge_insert_field_at(owf_obj *obj, int para, size_t offset, const char *column);

/* A page number (OWF_FIELD_PAGE) or the number of pages (OWF_FIELD_PAGES)
 * put at a place in an object's text, as opp_merge_insert_field_at puts a
 * merge field. Each page shows its own number, also when the field is on a
 * master. OWF_OK, OWF_ERR_FORMAT (no object, another field, no such place)
 * or OWF_ERR_MEMORY. */
int opp_insert_page_field_at(owf_obj *obj, int para, size_t offset, owf_field which);

/* A data source read from a CSV file (at most OW_MERGE_MAX_BYTES). OWF_OK
 * with *table set; OWF_ERR_IO (not read), OWF_ERR_UNSUPPORTED (too big),
 * OWF_ERR_FORMAT (no columns: its first line names them) or OWF_ERR_MEMORY. */
int opp_merge_load_file(const char *path, ow_merge_table **table);

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

/* A catalog merge's area on its page: the first item's box (twips from the
 * page's top left), repeated across and down the page, one record each.
 * The box takes in the space between items. */
#define OPP_CATALOG_MAX 64
typedef struct {
    int x, y, width, height;
    int across, down;                                /* 1..OPP_CATALOG_MAX */
} opp_catalog;
/* Catalog merge, in place: page (from 0) becomes as many pages as records
 * first..last need (first -1: the first, last -1: the last), each holding
 * across x down items left to right, then down. The item is the page's
 * objects whose boxes lie wholly inside the area's first box; each record's
 * copy moves by whole boxes, and a page's last items may stay empty. The
 * page's other objects come on each page with the page's first record.
 * Merge fields become plain text, every frame of a text flow keeps to its
 * own copy (a flow from a frame inside the item to one outside it is split
 * there: the item's part flows in each item, the rest on each page), and
 * the deck's other pages and masters are left as they are.
 * *made (when not NULL): the pages made. OWF_OK; OWF_ERR_UNSUPPORTED: no
 * table or records, no such page, an area or range that cannot be;
 * OWF_ERR_FORMAT: no object inside the area's first box; OWF_ERR_MEMORY:
 * the deck is as it was. */
int opp_merge_catalog(owf_deck *deck, const ow_merge_table *t, int page, const opp_catalog *cat,
                      int first, int last, int *made);
#ifdef __cplusplus
}
#endif
#endif
