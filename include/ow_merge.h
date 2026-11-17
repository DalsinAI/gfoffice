/*
 * A mail merge's data source as a table (src/core/ow_merge.c), shared by
 * GFOpenWrite and GFOpenPressmaster. MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef OW_MERGE_H
#define OW_MERGE_H

#include <stddef.h>
#include "owf.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ow_merge_table {
    int ncols, nrows;            /* nrows: the records, not counting the names' row */
    char **cells;                /* (nrows + 1) * ncols, row 0 the names; NULL: empty */
} ow_merge_table;

/* The largest data source read (bytes). */
#define OW_MERGE_MAX_BYTES (4UL * 1024UL * 1024UL)

/* A table from CSV text in UTF-8: the separator is the most of comma,
 * semicolon and tab on the first line; quotes as in RFC 4180; empty lines
 * and rows of empty cells are no records; a column without a name gets
 * "ColumnN". NULL when it has no columns, or memory ran out (*memory 1). */
ow_merge_table *ow_merge_table_parse(const char *s, size_t n, int *memory);
/* The same from bytes that are UTF-8 or else Latin-1. */
ow_merge_table *ow_merge_table_from_text(const char *data, size_t length, int *memory);
void ow_merge_table_free(ow_merge_table *t);
/* The bytes as UTF-8 (from owf_malloc, NUL after): the same, or Latin-1 made UTF-8. */
char *ow_merge_utf8(const char *data, size_t n, size_t *out_n);
/* A column by its name (exactly, then without minding ASCII case); -1: none. */
int ow_merge_column(const ow_merge_table *t, const char *name);
/* A record's value in a column ("" for an empty cell); NULL: no such record or column. */
const char *ow_merge_value(const ow_merge_table *t, int record, const char *column);

#ifdef __cplusplus
}
#endif
#endif
