/*
 * GFOpenBase's spelling through a form's own words (src/base/spell/ob_spell.c, openbase_spell.h) with GFOffice's
 * one spell checker (owf_spell.c on the host): labels, boxes' titles and buttons' words in the form's order,
 * wrapping round, changes kept to a label's size, and ARexx SPELL over the saved forms.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "openbase_spell.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static ob_form f;
static ob_table t;

int main(void)
{
    char words[128], db[128], journal[140], err[OB_ERR_MAX], out[256], longer[OB_LABEL_MAX + 8];
    ob_spell_hit hit;
    owf_spell *spell;
    ob_project *p;
    ob_fitem *it;
    FILE *fp;
    snprintf(words, sizeof words, "/tmp/ob-spell-%ld.words", (long)getpid());
    snprintf(db, sizeof db, "/tmp/ob-spell-%ld.oadb", (long)getpid());
    snprintf(journal, sizeof journal, "%s-journal", db);
    if (!(fp = fopen(words, "wb"))) return 2;
    fputs("customer\ncard\nname\nnext\nsave\ndetails\nthe\n", fp);
    fclose(fp);
    spell = owf_spell_open(words, NULL, NULL, 0);
    CHECK(spell != NULL);
    if (!spell) return 1;

    ob_form_init(&f, "Card", "Customers");
    CHECK(ob_spell_next(&f, spell, 0, 0, &hit) == 0);                    /* no items */
    it = ob_form_add(&f, OB_FI_LABEL, 0, 0, 20, 1);
    strcpy(it->text, "Custmer card");
    it = ob_form_add(&f, OB_FI_FIELD, 0, 1, 40, 1);
    strcpy(it->field, "Name");
    strcpy(it->text, "Nmae");                                            /* a field's own: not checked */
    it = ob_form_add(&f, OB_FI_BOX, 0, 2, 30, 5);
    strcpy(it->text, "Detials");
    ob_form_add_button(&f, "Nxt", OB_FA_NEXT, NULL, 0, 8);
    ob_form_add_button(&f, "Save", OB_FA_SAVE, NULL, 20, 8);
    ob_form_add(&f, OB_FI_LINE, 0, 9, 20, 1);

    CHECK(ob_spell_next(&f, spell, -1, 0, &hit) == 1 && hit.item == 0 && !strcmp(hit.word, "Custmer") &&
          hit.start == 0 && hit.end == 7);
    CHECK(ob_spell_next(&f, spell, hit.item, hit.end, &hit) == 1 && hit.item == 2 && !strcmp(hit.word, "Detials"));
    CHECK(ob_spell_next(&f, spell, hit.item, hit.end, &hit) == 1 && hit.item == 3 && !strcmp(hit.word, "Nxt"));
    CHECK(ob_spell_next(&f, spell, hit.item, hit.end, &hit) == 1 && hit.item == 0);   /* round again */
    CHECK(ob_spell_next(&f, spell, 5, 0, &hit) == 1 && hit.item == 0);               /* from the line on */

    CHECK(ob_spell_next(&f, spell, 0, 0, &hit) == 1);
    CHECK(ob_spell_replace(&f, &hit, "Customer") == OWF_OK && !strcmp(f.items[0].text, "Customer card"));
    CHECK(ob_spell_replace(&f, &hit, "Customer") == OWF_ERR_UNSUPPORTED);         /* stale */
    CHECK(ob_spell_next(&f, spell, 3, 0, &hit) == 1 && !strcmp(hit.word, "Nxt"));
    memset(longer, 'x', OB_LABEL_MAX);
    longer[OB_LABEL_MAX] = 0;
    CHECK(ob_spell_replace(&f, &hit, longer) == OWF_ERR_FORMAT && !strcmp(f.items[3].text, "Nxt"));
    CHECK(ob_spell_replace(&f, &hit, "Next") == OWF_OK && !strcmp(f.items[3].text, "Next"));
    hit.item = 1;
    CHECK(ob_spell_replace(&f, &hit, "x") == OWF_ERR_UNSUPPORTED);                /* a field item */
    hit.item = 99;
    CHECK(ob_spell_replace(&f, &hit, "x") == OWF_ERR_FORMAT);

    /* ARexx SPELL over the saved forms */
    remove(db);
    remove(journal);
    p = ob_open(db, OB_OPEN_CREATE, err, sizeof err);
    CHECK(p != NULL);
    if (p) {
        ob_table_init(&t, "Customers");
        ob_table_add_field(&t, "Name", OB_TEXT);
        CHECK(ob_table_create(p, &t));
        CHECK(ob_form_save(p, &f));
        CHECK(ob_spell_rexx(p, spell, "", out, sizeof out) == 0 && !strcmp(out, "1"));    /* Detials */
        CHECK(ob_spell_rexx(p, spell, "LIST", out, sizeof out) == 0 && !strcmp(out, "Card 3 Detials"));
        CHECK(ob_spell_rexx(p, spell, "list", out, 8) == 0 && !strcmp(out, ""));
        CHECK(ob_spell_rexx(p, spell, "IGNORE Detials", out, sizeof out) == 0);
        CHECK(ob_spell_rexx(p, spell, "COUNT", out, sizeof out) == 0 && !strcmp(out, "0"));
        CHECK(ob_spell_rexx(p, spell, "CHECK card", out, sizeof out) == 0 && !strcmp(out, "1"));
        CHECK(ob_spell_rexx(p, spell, "SPIN", out, sizeof out) == 10);
        CHECK(ob_spell_rexx(p, NULL, "COUNT", out, sizeof out) == 5);
        ob_close(p);
    }
    CHECK(ob_spell_rexx(NULL, spell, "COUNT", out, sizeof out) == 10);
    CHECK(ob_spell_next(NULL, spell, 0, 0, &hit) == -1 && ob_spell_replace(NULL, &hit, "x") == OWF_ERR_FORMAT);

    owf_spell_close(spell);
    remove(words);
    remove(db);
    remove(journal);
    if (fails) {
        printf("ob_spell: %d of %d checks failed\n", fails, checks);
        return 1;
    }
    printf("ob_spell: %d checks passed\n", checks);
    return 0;
}
