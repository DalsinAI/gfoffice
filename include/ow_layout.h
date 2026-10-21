/*
 * OpenWrite's pages on OpenLayout: an owf_doc as OpenLayout's tree (each
 * paragraph a block, each run an inline node, tables as grids), laid out
 * into pages at full resolution, and the editor's positions mapped to
 * OpenLayout's and back, for the caret and the pointer.
 *
 * Fonts reach the measure callback and the renderer as OpenLayout font
 * numbers: the document's font index plus one (OW_LAYOUT_FONT), 0 being
 * the document's default font.
 *
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#ifndef OW_LAYOUT_H
#define OW_LAYOUT_H

#include "openwrite_core.h"
#include "openlayout.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OW_LAYOUT_FONT(index) ((unsigned)((index) + 1))
#define OW_LAYOUT_FONT_INDEX(number) ((int)(number) - 1)

typedef struct ow_layout ow_layout;

/* The document's body as OpenLayout's tree, laid out into its pages.
 * The document must not change while the layout is used; lay it out again
 * after an edit. NULL when memory runs out. */
ow_layout *ow_layout_new(const owf_doc *doc, ol_measure_text_fn measure,
                         void *measure_userdata);
void ow_layout_free(ow_layout *layout);

ol_document *ow_layout_body(const ow_layout *layout);
int ow_layout_page_count(const ow_layout *layout);
/* The page in OpenLayout units (twips times 4). */
const ol_page *ow_layout_page(const ow_layout *layout);

/* An editor position to OpenLayout's, and back. 0 when there is none. */
int ow_layout_to_text(const ow_layout *layout, ow_position pos, ol_text_position *out);
int ow_layout_from_text(const ow_layout *layout, const ol_text_position *in, ow_position *out);

/* The body paragraph and run a node stands for: run -1 for a paragraph's
 * own block, a table's or a row's. 0 for a node outside the body. */
int ow_layout_node_run(const ow_layout *layout, ol_id node_id, int *paragraph, int *run);

/* The caret for an editor position: its page and box (page coordinates). */
int ow_layout_caret(const ow_layout *layout, ow_position pos, int *page, ol_rect *box);
/* The editor position nearest a point on a page. */
int ow_layout_hit(const ow_layout *layout, int page, ol_unit x, ol_unit y, ow_position *pos);

/* The header or footer for one page, fields filled in (the page number in
 * the document's style; date and time as given, may be NULL), laid out at
 * the body's width. *x, *y (either may be NULL) is where its top left goes
 * on the page: the header half way into the top margin, the footer ending
 * half way into the bottom one. NULL when the story is empty or on a first
 * page that has none. ol_document_free() it. */
ol_document *ow_layout_story(const ow_layout *layout, ow_story_kind which, int page,
                             const char *date, const char *time, ol_unit *x, ol_unit *y);

/* A page number as the document writes it: 1, iv, IV, d or D. */
void ow_layout_page_number(const owf_page *page, int number, char *out, size_t size);

#ifdef __cplusplus
}
#endif

#endif
