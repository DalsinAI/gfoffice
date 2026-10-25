/*
 * libowf: what the deck code shares inside the library (owf_deck.c,
 * in_odp.c, out_odp.c). Not part of the public interface.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#ifndef OWF_DECK_INT_H
#define OWF_DECK_INT_H

#include "owf_deck.h"
#include "owf_internal.h"

/* Limits, so that a damaged or hostile file costs bounded memory. */
#define OWF_DECK_MAX_SLIDES 2000
#define OWF_DECK_MAX_OBJS 4000          /* on one slide, master or layout */
#define OWF_DECK_MAX_MASTERS 64
#define OWF_DECK_MAX_LAYOUTS 256
#define OWF_DECK_KEEP_LIMIT (8UL * 1024 * 1024)   /* kept XML and parts, all told */
#define OWF_DECK_MAX_DEPTH 32           /* groups in groups, lists in lists */

/* The deck formats (in_odp.c, out_odp.c). */
extern const owf_deck_format owf_deck_format_odp, owf_deck_format_fodp;
int owf_export_odp(const owf_deck *deck, unsigned char **data, size_t *length, owf_report *report);
int owf_export_fodp(const owf_deck *deck, unsigned char **data, size_t *length, owf_report *report);

/* A story's paragraphs and runs freed (the story is empty after). */
void owf_deck_free_story(owf_story *story);
/* A copy of text (malloc), or NULL. */
char *owf_deck_strdup(const char *s, size_t length);

/* sin and cos of tenths of a degree, times 16384 (no floating point). */
long owf_deck_sin(int tenths);
long owf_deck_cos(int tenths);

/* A picture's own size in twips, from its PNG or JPEG header (its
 * resolution, else 96 dots an inch); 0 when it is not known. */
int owf_deck_image_size(const owf_image *image, int *width, int *height);

/* base64, for pictures in flat XML. */
void owf_deck_base64(owf_buf *out, const unsigned char *data, size_t length);

/* The master levels' defaults: Impress-like sizes and bullets. */
void owf_deck_default_levels(owf_master *master, int font);

#endif
