/*
 * openproject_spell.h: spelling through a plan in GFOpenProject (src/project/opj_spell.c), with GFOffice's one
 * spell checker (owfspell.library, libowf/include/owf_spell.h). Each task's name, then its notes, row by row.
 * Resources' names are people's and are not checked.
 * Plain C99. MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef OPENPROJECT_SPELL_H
#define OPENPROJECT_SPELL_H

#include "openproject_core.h"
#include "owf_spell.h"

enum { OPJ_SPELL_NAME, OPJ_SPELL_NOTES };

typedef struct {
    int row;                     /* the task */
    int field;                   /* OPJ_SPELL_NAME or OPJ_SPELL_NOTES */
    size_t start, end;           /* the word's bytes in that text */
    char word[96];
} opj_spell_hit;

/* The next misspelt word: in row's field from byte from, then the fields and rows after it, wrapping round to
 * the start of row's field (row out of range: from the first task). 1 with hit filled, 0 when the plan is
 * clean, -1 for bad input. */
int opj_spell_next(const opj_doc *doc, owf_spell *spell, int row, int field, size_t from, opj_spell_hit *hit);
/* Puts with in place of hit's word, one Undo step. OWF_OK; OWF_ERR_UNSUPPORTED when the text no longer has
 * that word there; OWF_ERR_FORMAT, OWF_ERR_MEMORY. */
int opj_spell_replace(opj_doc *doc, const opj_spell_hit *hit, const char *with);
/* ARexx SPELL, args being what follows the command word:
 *   SPELL [COUNT]   how many misspelt words the plan's names and notes have
 *   SPELL LIST      one line each, "id NAME word" or "id NOTES word" (id the task's row number from 1), as
 *                   many as fit
 *   SPELL CHECK, SUGGEST, ADD or IGNORE word, as every program has (owf_spell_command)
 * The answer, or the reason, goes in out; the return is ARexx's code (0, 5, 10 or 20). */
int opj_spell_rexx(const opj_doc *doc, owf_spell *spell, const char *args, char *out, size_t size);

#endif
