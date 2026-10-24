#ifndef OPENWRITE_PRINT_H
#define OPENWRITE_PRINT_H

#include <stddef.h>
#include "owf.h"

/* Print the document. With OpenPrint on the disk (C:OpenPrint), the document
 * is exported as a PDF to T: (named after name, the document's file, when
 * given) and OpenPrint's Print window opens on it. Without it, the text goes
 * through classic printer.device and the driver the Printer preferences name.
 *
 * Returns 1 on success. On failure, status receives a short user-facing
 * explanation.
 */
int ow_print_document_named(const owf_doc *doc, const char *name, char *status, size_t status_size);
int ow_print_document(const owf_doc *doc, char *status, size_t status_size);   /* no name */

#endif
