#ifndef OW_CLIPBOARD_H
#define OW_CLIPBOARD_H

#include <stddef.h>

/* Amiga clipboard.device FTXT exchange. Text at the API boundary is UTF-8;
 * FTXT CHRS is ISO-8859-1, so characters outside Latin-1 become '?'. The
 * returned buffer is malloc-owned. */
char *ow_clipboard_get_text(size_t *length);
int ow_clipboard_set_text(const char *utf8, size_t length);

/* A GFOffice deck-object payload: ODP bytes plus the source slide/object
 * indexes. The returned ODP buffer is malloc-owned. This custom IFF form lets
 * Publisher and Presentation exchange complete objects while FTXT remains
 * interoperable with other applications. */
unsigned char *ow_clipboard_get_deck_object(size_t *length, int *slide, int *obj);
int ow_clipboard_set_deck_object(const void *odp, size_t length, int slide, int obj);

#endif
