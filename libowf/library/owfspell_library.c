/*
 * owfspell.library 1.1, GFOffice's one spell checker (docs/suite/owf-spell.md):
 * libowf/src/owf_spell.c for every GFOffice program, so none carries a copy
 * and a word list is loaded once however many programs have it open.
 *
 * Its calls are owfspell_lib.sfd's (owfspell_calls.h, made from it by
 * scripts/gen-lib-calls.py); the rest is owf_libbase.h, as for the four
 * document libraries, but without their 17 shared calls.
 *
 * 1.1: owf_spell_command, ARexx SPELL's CHECK, SUGGEST, ADD and IGNORE for every
 * program.
 *
 * Copyright (c) 2026 AmiCradle Team. MIT.
 */
#define OWF_LIB_NAME "owfspell.library"
#define OWF_LIB_VERSION 1
#define OWF_LIB_REVISION 1
#define OWF_LIB_VERS "1.1"
#define OWF_LIB_DATE "17.11.2026"
#define OWF_LIB_CALLS_H "owfspell_calls.h"
#include "owf_spell.h"
#include "owf_libbase.h"
