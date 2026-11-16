#ifndef OPENWRITE_PRINT_H
#define OPENWRITE_PRINT_H

#include <stddef.h>
#include "owf.h"

/* Print the document. With GFOpenPrint on the disk (C:GFOpenPrint), the document
 * is exported as a PDF to T: (named after name, the document's file, when
 * given) and GFOpenPrint's Print window opens on it. Without it, the text goes
 * through classic printer.device and the driver the Printer preferences name.
 *
 * Returns 1 on success. On failure, status receives a short user-facing
 * explanation.
 */
int ow_print_document_named(const owf_doc *doc, const char *name, char *status, size_t status_size);
int ow_print_document(const owf_doc *doc, char *status, size_t status_size);   /* no name */
/* The same, with comments nonzero: the comments printed too, a raised mark where each is and the list after the
 * text (GFOpenPrint's PDF is libowf's "pdf-comments"); 2 (2.0.40): beside their lines ("pdf-comments-margin").
 * printer.device's plain text leaves them out, as before. */
int ow_print_document_comments(const owf_doc *doc, const char *name, int comments, char *status,
                               size_t status_size);

#endif
