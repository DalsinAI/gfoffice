#ifndef OWF_USE_SPELL_LIBRARY
#define OWF_USE_SPELL_LIBRARY
#endif
/*
 * owfspell.library for a program (libowf/include/owf_spell.h): its base,
 * opened the first time the program wants spelling, closed as it quits.
 * Every GFOffice program that spells links this and is built with
 * OWF_USE_SPELL_LIBRARY, so the spell checker is the library's, not a copy.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <exec/types.h>
#include <proto/exec.h>

#include "owf_spell.h"

struct Library *OWFSpellBase;

int owf_spell_library(void)
{
    if (!OWFSpellBase) {
        OWFSpellBase = OpenLibrary((CONST_STRPTR)"owfspell.library", 1);
        /* 1.1 at least: owf_spell_command */
        if (OWFSpellBase && OWFSpellBase->lib_Version == 1 && OWFSpellBase->lib_Revision < 1) {
            CloseLibrary(OWFSpellBase);
            OWFSpellBase = NULL;
        }
    }
    return OWFSpellBase != NULL;
}

void owf_spell_library_close(void)
{
    if (OWFSpellBase) CloseLibrary(OWFSpellBase);
    OWFSpellBase = NULL;
}
