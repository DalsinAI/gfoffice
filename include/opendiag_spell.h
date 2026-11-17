/*
 * opendiag_spell.h: spelling through a page of GFOpenDiag (src/diag/odg_spell.c), with GFOffice's one spell
 * checker (owfspell.library, libowf/include/owf_spell.h). Shapes' and connectors' labels are checked in the
 * page's order (drawing order); a layer's name is not. An HTML label (html=1) is checked as the text it shows.
 * This file is the program's, not owfdiag.library's: the library doesn't open owfspell.library itself.
 * Plain C99. MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef OPENDIAG_SPELL_H
#define OPENDIAG_SPELL_H

#include "opendiag_core.h"
#include "owf_spell.h"

typedef struct {
    char id[64];                 /* the shape's or connector's */
    size_t start, end;           /* the word's bytes in the label's text (owf_dlabel_text) */
    char word[96];
} odg_spell_hit;

/* The next misspelt word: in the cell id (NULL or not found: the page's first) from byte from of its text,
 * then the cells after it, wrapping round to the start of id's text. 1 with hit filled, 0 when the page is
 * clean, -1 for bad input or no memory. */
int odg_spell_next(const odg_doc *doc, owf_spell *spell, const char *id, size_t from, odg_spell_hit *hit);
/* Puts with in place of hit's word, one Undo step (odg_set_value). An HTML label keeps its markup: the word
 * is changed where it stands between the tags. OWF_OK; OWF_ERR_UNSUPPORTED when the label no longer has that
 * word there (or, in HTML, it is written with an entity); OWF_ERR_FORMAT, OWF_ERR_MEMORY. */
int odg_spell_replace(odg_doc *doc, const odg_spell_hit *hit, const char *with);
/* ARexx SPELL, args being what follows the command word:
 *   SPELL [COUNT]   how many misspelt words the page has
 *   SPELL LIST      one line each, "id word", as many as fit
 *   SPELL CHECK, SUGGEST, ADD or IGNORE word, as every program has (owf_spell_command)
 * The answer, or the reason, goes in out; the return is ARexx's code (0, 5, 10 or 20). */
int odg_spell_rexx(odg_doc *doc, owf_spell *spell, const char *args, char *out, size_t size);

#endif
