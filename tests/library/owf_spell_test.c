/*
 * OWFSpellTest: owfspell.library from a program, as GFOffice's programs use
 * it (OWF_USE_SPELL_LIBRARY, libowf/amiga/owf_spell_base.c). Opens the word
 * list beside it (PROGDIR:Dictionaries/en_GB.words) twice, as two programs
 * would, checks the second open shares the first's list (it takes far less
 * memory), and runs every call. Prints PASS or FAIL lines; returns 0, or 10
 * when a check failed, 20 when the library or list is missing.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <exec/memory.h>
#include <proto/exec.h>
#include <stdio.h>
#include <string.h>

#include "owf_spell.h"

static int failures;

static void check(int ok, const char *what)
{
    printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) failures++;
}

int main(void)
{
    char status[128], sug[4][OWF_SPELL_WORD];
    owf_spell *a, *b;
    owf_spell_range r[4];
    ULONG before, after_a, after_b;
    const char *t = "The colour of the wrold";
    size_t s0, s1;

    if (!owf_spell_library()) {
        puts("FAIL owfspell.library 1 not in LIBS:");
        return 20;
    }
    printf("owfspell.library %d.%d\n", OWFSpellBase->lib_Version, OWFSpellBase->lib_Revision);
    before = AvailMem(MEMF_ANY);
    a = owf_spell_open_default(status, sizeof status);
    after_a = AvailMem(MEMF_ANY);
    if (!a) {
        printf("FAIL %s\n", status);
        owf_spell_library_close();
        return 20;
    }
    printf("first open: %s (%lu bytes)\n", status, (unsigned long)(before - after_a));
    b = owf_spell_open("PROGDIR:Dictionaries/en_GB.words", NULL, status, sizeof status);
    after_b = AvailMem(MEMF_ANY);
    check(b != NULL, "second open");
    printf("second open: %lu bytes\n", (unsigned long)(after_a - after_b));
    check(after_a - after_b < (before - after_a) / 10, "the second open shares the first's word list");
    check(owf_spell_count(a) > 100000 && owf_spell_count(a) == owf_spell_count(b), "over 100,000 words, both");
    check(owf_spell_word_ok(a, "colour", 6) && !owf_spell_word_ok(a, "wrold", 5), "owf_spell_word_ok");
    check(owf_spell_scan(b, t, 0, strlen(t), &s0, &s1) && s0 == 18 && s1 == 23, "owf_spell_scan");
    check(owf_spell_words(a, t, strlen(t), r, 4) == 1 && r[0].start == 18, "owf_spell_words");
    check(owf_spell_suggest(a, "wrold", sug[0], 4) >= 1, "owf_spell_suggest");
    check(owf_spell_ignore(a, "amicradle") && owf_spell_word_ok(a, "AmiCradle", 9) &&
              !owf_spell_word_ok(b, "amicradle", 9),
          "owf_spell_ignore, the one opener only");
    check(owf_spell_add(b, "gfoffice") && owf_spell_word_ok(b, "GFOffice", 8), "owf_spell_add (no user list)");
    owf_spell_close(a);
    check(owf_spell_word_ok(b, "colour", 6), "the list stays for the other opener");
    owf_spell_close(b);
    owf_spell_library_close();
    printf("%s: %d failed\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 10 : 0;
}
