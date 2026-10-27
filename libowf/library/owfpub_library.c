/*
 * owfpub.library 3.1, OpenPublisher's part of libowf: Publisher files. The
 * .pub reader and its compound-file reader, with the document model its text
 * uses.
 *
 * Its calls are owfpub_lib.sfd's (owfpub_calls.h, made from it by
 * scripts/gen-lib-calls.py); the rest is owf_libbase.h. The first 17 calls
 * are the same in all four of GFOffice's libraries
 * (docs/suite/owf-libraries.md).
 *
 * 3.1: owf_doc.header_left and footer_left (owfwrite.library 3.2), so the
 * document model's owf_doc is larger and a program's size check refuses
 * 3.0. No call changes.
 *
 * Copyright (c) 2026 AmigaChrome Team. MIT.
 */
#define OWF_LIB_NAME "owfpub.library"
#define OWF_LIB_VERSION 3
#define OWF_LIB_REVISION 1
#define OWF_LIB_VERS "3.1"
#define OWF_LIB_DATE "27.10.2026"
#define OWF_LIB_CALLS_H "owfpub_calls.h"
#include "owf_libbase.h"
