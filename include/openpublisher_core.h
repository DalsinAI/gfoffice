/*
 * GFOpenPublisher's portable core.  The first reusable slice turns libowf's
 * clean-room Publisher model into the suite's shared positioned-page model,
 * so GFOpenPresentation's GFOpenLayout/PDF pipeline can display and print it.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef OPENPUBLISHER_CORE_H
#define OPENPUBLISHER_CORE_H

#include "owf_pub.h"
#include "owf_deck.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OPP_CORE_VERSION "0.3.0"

/* A deep conversion: pub remains owned by the caller and may be freed at
 * once.  The returned deck owns independent fonts, pictures and stories.
 * P1a preserves page/master assignment, frame order/grouping, boxes, plain
 * text, tables' dimensions and pictures; formatting is a later reader step. */
int opp_pub_to_deck(const owf_pub *pub, owf_deck **deck, owf_report *report);
int opp_import_pub_file(const char *path, owf_deck **deck, owf_report *report, int *version);

/* GFOpenPublisher save: a deck (as opp_pub_to_deck makes, or as edited)
 * back into libowf's Publisher model, and through owf_pub_write a .pub.
 * Frames sharing a flow_story share one story, its text the first frame's;
 * boxes are kept in twips, looks, tables, groups and pictures as the deck
 * has them. The pub and data are the caller's (owf_pub_free, owf_free).
 * OWF_OK, OWF_ERR_FORMAT, OWF_ERR_MEMORY or OWF_ERR_IO. */
int opp_deck_to_pub(const owf_deck *deck, owf_pub **pub, owf_report *report);
int opp_export_pub(const owf_deck *deck, unsigned char **data, size_t *length, owf_report *report);
int opp_export_pub_file(const owf_deck *deck, const char *path, owf_report *report);

/* Count shared text flows in a publication deck. A flow is two or more
 * text objects with the same non-negative owf_obj.flow_story, in page/object
 * order. When frames is not NULL it receives the number of frames belonging
 * to those flows. Masters are deliberately excluded: their objects repeat
 * on pages rather than continuing a page story. */
int opp_flow_chain_count(const owf_deck *deck, int *frames);

#ifdef __cplusplus
}
#endif
#endif
