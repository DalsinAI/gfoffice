/*
 * GFOpenProject's spelling through a plan (src/project/opj_spell.c, openproject_spell.h) with GFOffice's one
 * spell checker (owf_spell.c on the host): names then notes row by row, wrapping round, a change as one Undo
 * step, and ARexx SPELL.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "openproject_spell.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

int main(void)
{
    char path[128], out[256];
    opj_spell_hit hit;
    owf_spell *spell;
    opj_doc *doc;
    FILE *f;
    snprintf(path, sizeof path, "/tmp/opj-spell-%ld.words", (long)getpid());
    if (!(f = fopen(path, "wb"))) return 2;
    fputs("plan\nbuild\nship\nthe\nwith\nsupplier\ncheck\n", f);
    fclose(f);
    spell = owf_spell_open(path, NULL, NULL, 0);
    CHECK(spell != NULL);
    if (!spell) return 1;
    doc = opj_doc_new(NULL, owf_ptime_make(2026, 11, 16, 8, 0));
    CHECK(doc != NULL);
    if (!doc) return 1;
    CHECK(opj_spell_next(doc, spell, 0, OPJ_SPELL_NAME, 0, &hit) == 0);       /* no tasks */
    CHECK(opj_insert_task(doc, 0, "Plan") == OWF_OK);
    CHECK(opj_insert_task(doc, 1, "Biuld it") == OWF_OK);
    CHECK(opj_insert_task(doc, 2, "Ship") == OWF_OK);
    CHECK(opj_set_notes(doc, 0, "Check with the suplier") == OWF_OK);
    CHECK(opj_set_notes(doc, 2, "teh end") == OWF_OK);

    /* names then notes, row by row */
    CHECK(opj_spell_next(doc, spell, -1, 0, 0, &hit) == 1 && hit.row == 0 && hit.field == OPJ_SPELL_NOTES &&
          !strcmp(hit.word, "suplier") && hit.start == 15 && hit.end == 22);
    CHECK(opj_spell_next(doc, spell, hit.row, hit.field, hit.end, &hit) == 1 && hit.row == 1 &&
          hit.field == OPJ_SPELL_NAME && !strcmp(hit.word, "Biuld"));
    CHECK(opj_spell_next(doc, spell, hit.row, hit.field, hit.end, &hit) == 1 && hit.row == 1 && !strcmp(hit.word, "it"));
    CHECK(opj_spell_next(doc, spell, hit.row, hit.field, hit.end, &hit) == 1 && hit.row == 2 &&
          hit.field == OPJ_SPELL_NOTES && !strcmp(hit.word, "teh"));
    CHECK(opj_spell_next(doc, spell, hit.row, hit.field, hit.end, &hit) == 1 && !strcmp(hit.word, "end"));
    /* round again */
    CHECK(opj_spell_next(doc, spell, hit.row, hit.field, hit.end, &hit) == 1 && hit.row == 0 &&
          !strcmp(hit.word, "suplier"));
    /* from the middle of row 0's notes: on to the rest, then back to its start, up to there */
    CHECK(owf_spell_ignore(spell, "it") && owf_spell_ignore(spell, "end"));
    CHECK(opj_spell_next(doc, spell, 0, OPJ_SPELL_NOTES, 22, &hit) == 1 && !strcmp(hit.word, "Biuld"));

    /* a change: one Undo step, a stale one refused */
    CHECK(opj_spell_next(doc, spell, 1, OPJ_SPELL_NAME, 0, &hit) == 1 && !strcmp(hit.word, "Biuld"));
    CHECK(opj_spell_replace(doc, &hit, "Build") == OWF_OK && !strcmp(doc->plan->tasks[1].name, "Build it"));
    CHECK(opj_spell_replace(doc, &hit, "Build") == OWF_ERR_UNSUPPORTED);
    CHECK(opj_undo(doc) == OWF_OK && !strcmp(doc->plan->tasks[1].name, "Biuld it"));
    CHECK(opj_redo(doc) == OWF_OK);
    CHECK(opj_spell_next(doc, spell, 0, OPJ_SPELL_NAME, 0, &hit) == 1 && !strcmp(hit.word, "suplier"));
    CHECK(opj_spell_replace(doc, &hit, "supplier") == OWF_OK && !strcmp(doc->plan->tasks[0].notes, "Check with the supplier"));

    /* ARexx SPELL */
    CHECK(opj_spell_rexx(doc, spell, "", out, sizeof out) == 0 && !strcmp(out, "1"));        /* teh */
    CHECK(opj_spell_rexx(doc, spell, "list", out, sizeof out) == 0 && !strcmp(out, "3 NOTES teh"));
    CHECK(opj_set_cell(doc, 1, OPJ_COL_NAME, "Biuld") == OWF_OK);
    CHECK(opj_spell_rexx(doc, spell, "LIST", out, sizeof out) == 0 && !strcmp(out, "2 NAME Biuld\n3 NOTES teh"));
    CHECK(opj_spell_rexx(doc, spell, "LIST", out, 14) == 0 && !strcmp(out, "2 NAME Biuld"));
    CHECK(opj_spell_rexx(doc, spell, "CHECK ship", out, sizeof out) == 0 && !strcmp(out, "1"));
    CHECK(opj_spell_rexx(doc, spell, "ADD Biuld", out, sizeof out) == 0);
    CHECK(opj_spell_rexx(doc, spell, "COUNT", out, sizeof out) == 0 && !strcmp(out, "1"));
    CHECK(opj_spell_rexx(doc, spell, "SPIN", out, sizeof out) == 10);
    CHECK(opj_spell_rexx(doc, NULL, "COUNT", out, sizeof out) == 5);
    CHECK(opj_spell_rexx(NULL, spell, "COUNT", out, sizeof out) == 10);
    CHECK(opj_spell_next(NULL, spell, 0, 0, 0, &hit) == -1 && opj_spell_replace(doc, NULL, "x") == OWF_ERR_FORMAT);

    opj_doc_free(doc);
    owf_spell_close(spell);
    remove(path);
    if (fails) {
        printf("opj_spell: %d of %d checks failed\n", fails, checks);
        return 1;
    }
    printf("opj_spell: %d checks passed\n", checks);
    return 0;
}
