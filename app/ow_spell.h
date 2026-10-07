#ifndef OPENWRITE_SPELL_H
#define OPENWRITE_SPELL_H

#include <stddef.h>
#include "openwrite_core.h"

typedef struct ow_spell ow_spell;

ow_spell *ow_spell_open(const char *primary_path, const char *user_path,
                        char *status, size_t status_size);
void ow_spell_close(ow_spell *spell);

/* Finds the next misspelled word after the current caret/selection and selects
 * it. Returns 1 for a word, 0 when the document is clean, -1 on memory/error. */
int ow_spell_next(ow_spell *spell, ow_editor *editor, char *word,
                  size_t word_size, int wrap);

/* User dictionary words are kept in memory and appended to user_path. */
int ow_spell_add(ow_spell *spell, const char *word);

/* Small edit-distance suggestions. Returns the number written. */
int ow_spell_suggest(ow_spell *spell, const char *word,
                     char suggestions[][48], int max_suggestions);

#endif
