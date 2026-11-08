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

#define OPP_CORE_VERSION "0.2.0"

/* A deep conversion: pub remains owned by the caller and may be freed at
 * once.  The returned deck owns independent fonts, pictures and stories.
 * P1a preserves page/master assignment, frame order/grouping, boxes, plain
 * text, tables' dimensions and pictures; formatting is a later reader step. */
int opp_pub_to_deck(const owf_pub *pub, owf_deck **deck, owf_report *report);
int opp_import_pub_file(const char *path, owf_deck **deck, owf_report *report, int *version);

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
