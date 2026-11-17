#ifndef OPENWRITE_SPELL_H
#define OPENWRITE_SPELL_H

#include <stddef.h>
#include "openwrite_core.h"
#include "owf_spell.h"

/* Finds the next misspelled word after the current caret/selection and selects
 * it. Returns 1 for a word, 0 when the document is clean, -1 on memory/error. */
int ow_spell_next(owf_spell *spell, ow_editor *editor, char *word,
                  size_t word_size, int wrap);

#endif
