#ifndef OPENWRITE_PRINT_H
#define OPENWRITE_PRINT_H

#include <stddef.h>
#include "owf.h"

/* Submit the document through classic printer.device. This deliberately uses
 * the standard Amiga path so OpenAmigaPrint/OpenPrint, PostScript drivers and
 * real classic printer drivers all remain valid backends.
 *
 * Returns 1 on success. On failure, status receives a short user-facing
 * explanation.
 */
int ow_print_document(const owf_doc *doc, char *status, size_t status_size);

#endif
