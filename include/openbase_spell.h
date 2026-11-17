/*
 * openbase_spell.h: spelling through GFOpenBase's forms (src/base/spell/ob_spell.c), with GFOffice's one spell
 * checker (owfspell.library, libowf/include/owf_spell.h): the words a form shows of its own, its labels', boxes'
 * titles and buttons' words. Field names, macros and the rows' data are not checked.
 * Plain C99. MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef OPENBASE_SPELL_H
#define OPENBASE_SPELL_H

#include "openbase_core.h"
#include "owf.h"
#include "owf_spell.h"

typedef struct {
    int item;                    /* the form's item */
    size_t start, end;           /* the word's bytes in its text */
    char word[OB_LABEL_MAX];
} ob_spell_hit;

/* The next misspelt word of form f: in item from byte from (item out of range: from the first), then the items
 * after it, wrapping round to the start of item's text. 1 with hit filled, 0 when the form is clean, -1 for bad
 * input. */
int ob_spell_next(const ob_form *f, owf_spell *spell, int item, size_t from, ob_spell_hit *hit);
/* Puts with in place of hit's word in the form (in memory: the designer saves it). OWF_OK; OWF_ERR_UNSUPPORTED
 * when the item no longer has that word there; OWF_ERR_FORMAT when the text would be longer than a label can be
 * (OB_LABEL_MAX - 1 bytes) or there is no such item. */
int ob_spell_replace(ob_form *f, const ob_spell_hit *hit, const char *with);
/* ARexx SPELL, args being what follows the command word:
 *   SPELL [COUNT]   how many misspelt words the project's saved forms have
 *   SPELL LIST      one line each, "form item word" (item counted from 1), as many as fit
 *   SPELL CHECK, SUGGEST, ADD or IGNORE word, as every program has (owf_spell_command)
 * The answer, or the reason, goes in out; the return is ARexx's code (0, 5, 10 or 20). */
int ob_spell_rexx(ob_project *p, owf_spell *spell, const char *args, char *out, size_t size);

#endif
