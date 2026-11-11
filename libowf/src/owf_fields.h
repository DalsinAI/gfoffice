/*
 * libowf 1.7: fields with an argument or a shown value (owf.h's OWF_FIELD_TITLE to OWF_FIELD_REF_PAGE), the
 * document's author and subject, and its user fields, for the readers and writers that keep them (ODT, DOCX).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef OWF_FIELDS_H
#define OWF_FIELDS_H

#include "owf_internal.h"

/* A field run: arg is its href (a user field's or a bookmark's name), shown its text (the value last shown).
 * Either may be NULL. 0, or -1 when memory runs out. */
int owf_para_add_field(owf_para *para, const owf_charfmt *fmt, owf_field field, const char *arg, const char *shown);
void owf_builder_field(owf_builder *b, owf_field field, const char *arg, const char *shown);

/* The document's author and subject (NULL or "": none). 0, or -1 when memory runs out. */
int owf_doc_set_author(owf_doc *doc, const char *utf8);
int owf_doc_set_subject(owf_doc *doc, const char *utf8);

/* A user field set to value (a new one, or the one of that name changed). 0, or -1 when memory runs out. */
int owf_doc_set_user_field(owf_doc *doc, const char *name, const char *value);
/* The value of the user field of that name, NULL: none */
const char *owf_doc_user_field(const owf_doc *doc, const char *name);

/* A field kind's name in ARexx and the editor ("TITLE", "REFPAGE"); "" for none */
const char *owf_field_name(owf_field field);

#endif
