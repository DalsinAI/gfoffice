/*
 * GFOpenCalc's spelling (src/calc/oc_spell.c) with GFOffice's one spell
 * checker (libowf/src/owf_spell.c): the next misspelt word through a
 * sheet's text cells and back round, replacing it (undoable, protected
 * cells refused), and ARexx SPELL.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "opencalc_core.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int checks, failures;
#define CHECK(c, what)                                                \
    do {                                                              \
        checks++;                                                     \
        if (!(c)) {                                                   \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, what);      \
            failures++;                                               \
        }                                                             \
    } while (0)

static void put(oc_core *core, const char *cell, const char *input)
{
    unsigned row, col;
    oc_core_goto(core, cell);
    oc_core_active(core, &row, &col);
    oc_core_set_input(core, 0, row, col, input);
}

int main(void)
{
    char dict[256], out[256], text[256];
    FILE *f;
    owf_spell *sp;
    oc_core *core;
    oc_spell_hit hit;
    unsigned row, col;

    snprintf(dict, sizeof dict, "/tmp/oc-spell-%ld.words", (long)getpid());
    if (!(f = fopen(dict, "w"))) return 2;
    fputs("hello\nworld\nthe\ntotal\nsales\nnorth\n", f);
    fclose(f);
    sp = owf_spell_open(dict, NULL, NULL, 0);
    core = oc_core_new(NULL);
    if (!sp || !core) return 2;

    put(core, "A1", "Hello wrold");
    put(core, "B1", "12.5");             /* a number: not checked */
    put(core, "C1", "=\"zzqx\"&\"\"");   /* a formula: not checked */
    put(core, "A2", "The totl");
    put(core, "B3", "Norht sales");
    oc_core_goto(core, "A1");

    CHECK(oc_core_spell_next(core, sp, 0, &hit) == 1 && hit.row == 0 && hit.col == 0 && !strcmp(hit.word, "wrold") &&
              hit.start == 6 && hit.end == 11,
          "wrold in A1");
    CHECK(oc_core_spell_next(core, sp, hit.end, &hit) == 1 && hit.row == 1 && hit.col == 0 && !strcmp(hit.word, "totl"),
          "then totl in A2, past the number and the formula");
    oc_core_active(core, &row, &col);
    CHECK(row == 1 && col == 0, "A2 selected");
    CHECK(oc_core_spell_replace(core, &hit, "total") == OWF_OK, "replaced");
    CHECK(oc_core_cell_text(core, 0, 1, 0, text, sizeof text) && !strcmp(text, "The total"), "A2 reads The total");
    CHECK(oc_core_spell_replace(core, &hit, "total") == OWF_ERR_UNSUPPORTED, "the same hit twice is refused");
    CHECK(oc_core_spell_next(core, sp, 0, &hit) == 1 && hit.row == 2 && hit.col == 1 && !strcmp(hit.word, "Norht"),
          "Norht in B3");
    CHECK(oc_core_spell_next(core, sp, hit.end, &hit) == 1 && hit.row == 0 && !strcmp(hit.word, "wrold"),
          "round to A1");
    CHECK(oc_core_undo(core) == OWF_OK && oc_core_cell_text(core, 0, 1, 0, text, sizeof text) &&
              !strcmp(text, "The totl"),
          "Undo puts totl back");

    /* text that looks like a number stays text after a replacement */
    put(core, "D1", "1e5 helo");
    oc_core_goto(core, "D1");
    CHECK(oc_core_spell_next(core, sp, 0, &hit) == 1 && !strcmp(hit.word, "helo"), "helo in D1");
    CHECK(oc_core_spell_replace(core, &hit, "") == OWF_OK && oc_core_cell_text(core, 0, 0, 3, text, sizeof text) &&
              !strcmp(text, "1e5 "),
          "still text");

    CHECK(oc_spell_rexx(core, sp, "", out, sizeof out) == 0 && !strcmp(out, "3"), "SPELL: three");
    CHECK(oc_spell_rexx(core, sp, "LIST", out, sizeof out) == 0 && !strcmp(out, "A1 wrold\nA2 totl\nB3 Norht"),
          "SPELL LIST");
    CHECK(oc_spell_rexx(core, sp, "LIST", out, 12) == 0 && !strcmp(out, "A1 wrold"), "SPELL LIST cut to fit");
    CHECK(oc_spell_rexx(core, sp, "CHECK sales", out, sizeof out) == 0 && !strcmp(out, "1"), "SPELL CHECK");
    CHECK(oc_spell_rexx(core, sp, "IGNORE wrold", out, sizeof out) == 0 &&
              oc_spell_rexx(core, sp, "COUNT", out, sizeof out) == 0 && !strcmp(out, "2"),
          "SPELL IGNORE");
    CHECK(oc_spell_rexx(core, sp, "SPIN", out, sizeof out) == 10, "SPELL SPIN");
    CHECK(oc_spell_rexx(core, NULL, "COUNT", out, sizeof out) == 5, "no dictionary");
    CHECK(oc_core_spell_next(core, NULL, 0, &hit) == -1, "bad input");

    oc_core_free(core);
    owf_spell_close(sp);
    remove(dict);
    printf("calc spelling: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
