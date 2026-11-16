#ifndef OW_SPELL_DICT_H
#define OW_SPELL_DICT_H

/* The spelling dictionary (src/core/ow_spell_dict.c): a word list, one
 * word a line (a "/flags" ending and lines starting with a digit are
 * skipped, as a Hunspell .dic is), and the user's own words beside it. */

#include <stddef.h>

typedef struct ow_spell ow_spell;

ow_spell *ow_spell_open(const char *primary_path, const char *user_path,
                        char *status, size_t status_size);
void ow_spell_close(ow_spell *spell);

/* User dictionary words are kept in memory and appended to user_path. */
int ow_spell_add(ow_spell *spell, const char *word);

/* Small edit-distance suggestions. Returns the number written. */
int ow_spell_suggest(ow_spell *spell, const char *word,
                     char suggestions[][48], int max_suggestions);

/* 1 when the word (length bytes, any case) is in either list, or is one
 * the checker leaves alone: one letter, any digit in it, or all capitals
 * of five letters or fewer. */
int ow_spell_word_ok(ow_spell *spell, const char *word, size_t length);

/* The first misspelt word in text[from, to): letters, bytes from 0x80 (so
 * UTF-8 letters stay whole) and apostrophes, the apostrophes at its ends
 * left off. 1 with its bytes [*start, *end), 0 when there is none. */
int ow_spell_scan(ow_spell *spell, const char *text, size_t from, size_t to,
                  size_t *start, size_t *end);

#endif
