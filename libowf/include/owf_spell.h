/*
 * GFOffice's spelling: one dictionary and one set of word rules for every
 * GFOffice program (docs/suite/owf-spell.md). The source is
 * libowf/src/owf_spell.c. On the Amiga it is owfspell.library: a program
 * defines OWF_USE_SPELL_LIBRARY, links libowf/amiga/owf_spell_base.c and
 * calls owf_spell_library() before its first call; the calls below then go
 * through the library, and a dictionary opened by several programs at once
 * is loaded once. The host tests link owf_spell.c itself.
 *
 * The calls' names start with owf_spell_ and none is one of the four
 * document libraries' shared calls, so any file can use owfspell.library
 * beside owfwrite, owfcalc, owfpub or owfpresent.library.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef OWF_SPELL_H
#define OWF_SPELL_H

#include <stddef.h>

typedef struct owf_spell owf_spell;

/* One misspelt word: its bytes [start, end) of the text checked. */
typedef struct owf_spell_range {
    size_t start, end;
} owf_spell_range;

/* The longest suggestion, with its NUL: owf_spell_suggest's slots. */
#define OWF_SPELL_WORD 48

/* The dictionaries a program looks for, in order, and the user's own words
 * (owf_spell_open_default). */
#define OWF_SPELL_USER_PATH "PROGDIR:Dictionaries/user.words"

/* A dictionary: a word list, one word a line (a "/flags" ending and lines
 * starting with a digit are skipped, as a Hunspell .dic is), and the user's
 * own words beside it (user_path, may be NULL). status, when given, gets a
 * line for the program's status bar. NULL when primary_path can't be read. */
owf_spell *owf_spell_open(const char *primary_path, const char *user_path, char *status, size_t status_size);
/* The first of PROGDIR:Dictionaries/en_GB.words, PROGDIR:Dictionaries/en_US.words
 * and SYS:Dictionaries/en_GB.words, with OWF_SPELL_USER_PATH. */
owf_spell *owf_spell_open_default(char *status, size_t status_size);
void owf_spell_close(owf_spell *spell);

/* Into the user's words, kept in memory and added to user_path. 1 when it
 * is there afterwards. */
int owf_spell_add(owf_spell *spell, const char *word);
/* Ignore All: the word passes until this owf_spell is closed; not saved. */
int owf_spell_ignore(owf_spell *spell, const char *word);

/* Up to max suggestions of OWF_SPELL_WORD bytes each, in slots (max *
 * OWF_SPELL_WORD bytes): the dictionary's nearest words, one or two edits
 * away. Returns the number written. */
int owf_spell_suggest(owf_spell *spell, const char *word, char *slots, int max);

/* 1 when the word (length bytes, any case) is in the dictionary, the user's
 * words or the ignored ones, or is one the checker leaves alone: one
 * letter, any digit in it, or all capitals of five letters or fewer. */
int owf_spell_word_ok(owf_spell *spell, const char *word, size_t length);

/* The first misspelt word in text[from, to): letters, bytes from 0x80 (so
 * UTF-8 letters stay whole) and apostrophes, the apostrophes at its ends
 * left off. 1 with its bytes [*start, *end), 0 when there is none. */
int owf_spell_scan(owf_spell *spell, const char *text, size_t from, size_t to, size_t *start, size_t *end);

/* Every misspelt word of text[0, length): the first max_ranges into ranges
 * (may be NULL). Returns how many there are, all of them. */
int owf_spell_words(owf_spell *spell, const char *text, size_t length, owf_spell_range *ranges, int max_ranges);

/* The number of words in the dictionary (not the user's). */
int owf_spell_count(const owf_spell *spell);

#if defined(OWF_USE_SPELL_LIBRARY) && !defined(OWF_LIBRARY_BUILD)
/* owfspell.library, opened the first time (libowf/amiga/owf_spell_base.c):
 * 1 when it is open. A program calls owf_spell_library_close() as it quits. */
int owf_spell_library(void);
void owf_spell_library_close(void);
#include <proto/owfspell.h>
#else
static inline int owf_spell_library(void) { return 1; }
static inline void owf_spell_library_close(void) {}
#endif

#endif
