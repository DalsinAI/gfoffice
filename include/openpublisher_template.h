/*
 * GFOpenPublisher's built-in publication templates (src/pub/opp_template.c):
 * a new publication of a known kind (flyer, postcard, business card, folded
 * card, newsletter, letterhead, catalog page), its page size and its frames
 * made here, with the suite's own placeholder text and colours. MIT,
 * Copyright (c) 2026 AmiCradle Team.
 */
#ifndef OPENPUBLISHER_TEMPLATE_H
#define OPENPUBLISHER_TEMPLATE_H
#include "owf_deck.h"
#ifdef __cplusplus
extern "C" {
#endif

#define OPP_TEMPLATE_VERSION "0.1.0"

typedef struct {
    const char *name;                                /* "Flyer": what the New from Template list shows */
    const char *about;                               /* one line on what it is for */
    int width, height;                               /* the page, twips */
    int pages;                                       /* how many pages it starts with */
} opp_template_info;

/* How many templates there are, and template i's description (NULL out of range). */
int opp_template_count(void);
const opp_template_info *opp_template_info_at(int i);
/* The template named name (any case), or -1. */
int opp_template_find(const char *name);
/* A new publication from template i in *deck. OWF_OK; OWF_ERR_UNSUPPORTED:
 * no such template; OWF_ERR_MEMORY (*deck NULL). */
int opp_template_new(int i, owf_deck **deck);

#ifdef __cplusplus
}
#endif
#endif
