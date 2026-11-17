/*
 * GFOpenDiag's spelling through a page (src/diag/odg_spell.c, opendiag_spell.h) with GFOffice's one spell
 * checker (owf_spell.c on the host): labels in drawing order, layers' names left alone, HTML labels checked as
 * shown and changed between their tags, Undo, and ARexx SPELL.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "opendiag_spell.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static const char *value_of(odg_doc *doc, const char *id)
{
    owf_dpage *p = odg_page(doc);
    int i;
    for (i = 0; i < p->ncells; ++i)
        if (!strcmp(p->cells[i].id, id)) return p->cells[i].value ? p->cells[i].value : "";
    return NULL;
}

int main(void)
{
    char path[128], a[24], b[24], h[24], e[24], out[256];
    owf_diagram d;
    odg_doc doc;
    odg_spell_hit hit;
    owf_spell *spell;
    FILE *f;
    snprintf(path, sizeof path, "/tmp/odg-spell-%ld.words", (long)getpid());
    if (!(f = fopen(path, "wb"))) return 2;
    fputs("hello\nworld\nthe\nsales\ntotal\nstart\nend\nyes\nor\n", f);
    fclose(f);
    spell = owf_spell_open(path, NULL, NULL, 0);
    CHECK(spell != NULL);
    if (!spell) return 1;

    owf_diagram_init(&d);
    CHECK(owf_diagram_add_page(&d, NULL, "Page-1") != NULL);
    odg_init(&doc, &d, 0);
    CHECK(odg_layer_rename(&doc, 0, "Layr") == OWF_OK);            /* a layer's name: not checked */
    CHECK(odg_spell_next(&doc, spell, NULL, 0, &hit) == 0);
    CHECK(odg_add_shape(&doc, 0, 0, 10000, 5000, "rounded=1;", "Hello wrold", a) == OWF_OK);
    CHECK(odg_add_shape(&doc, 20000, 0, 10000, 5000, "html=1;", "<b>Teh</b> sales<br>totl &amp; teh", h) == OWF_OK);
    CHECK(odg_add_shape(&doc, 40000, 0, 10000, 5000, NULL, "End", b) == OWF_OK);
    CHECK(odg_add_edge(&doc, a, b, NULL, NULL, NULL, e) == OWF_OK);
    CHECK(odg_set_value(&doc, e, "yes or noo") == OWF_OK);

    /* in drawing order, from the start */
    CHECK(odg_spell_next(&doc, spell, NULL, 0, &hit) == 1 && !strcmp(hit.id, a) && !strcmp(hit.word, "wrold") &&
          hit.start == 6 && hit.end == 11);
    CHECK(odg_spell_next(&doc, spell, hit.id, hit.end, &hit) == 1 && !strcmp(hit.id, h) && !strcmp(hit.word, "Teh"));
    CHECK(odg_spell_next(&doc, spell, hit.id, hit.end, &hit) == 1 && !strcmp(hit.word, "totl"));
    CHECK(odg_spell_next(&doc, spell, hit.id, hit.end, &hit) == 1 && !strcmp(hit.word, "teh"));
    CHECK(odg_spell_next(&doc, spell, hit.id, hit.end, &hit) == 1 && !strcmp(hit.id, e) && !strcmp(hit.word, "noo"));
    /* round again: the first shape's word */
    CHECK(odg_spell_next(&doc, spell, hit.id, hit.end, &hit) == 1 && !strcmp(hit.id, a) && !strcmp(hit.word, "wrold"));
    /* an id not on the page: from the start */
    CHECK(odg_spell_next(&doc, spell, "nosuch", 99, &hit) == 1 && !strcmp(hit.id, a));

    /* a plain label */
    CHECK(odg_spell_next(&doc, spell, a, 0, &hit) == 1);
    CHECK(odg_spell_replace(&doc, &hit, "world") == OWF_OK && !strcmp(value_of(&doc, a), "Hello world"));
    CHECK(odg_spell_replace(&doc, &hit, "world") == OWF_ERR_UNSUPPORTED);   /* stale: the word is gone */
    CHECK(odg_undo(&doc) == OWF_OK && !strcmp(value_of(&doc, a), "Hello wrold"));
    CHECK(odg_redo(&doc) == OWF_OK);

    /* an HTML label: the second "teh", not the first, and the markup kept */
    CHECK(odg_spell_next(&doc, spell, h, 0, &hit) == 1 && !strcmp(hit.word, "Teh"));
    CHECK(odg_spell_replace(&doc, &hit, "The") == OWF_OK &&
          !strcmp(value_of(&doc, h), "<b>The</b> sales<br>totl &amp; teh"));
    CHECK(odg_spell_next(&doc, spell, h, 0, &hit) == 1 && !strcmp(hit.word, "totl"));
    CHECK(odg_spell_replace(&doc, &hit, "<i>total</i>") == OWF_ERR_UNSUPPORTED);  /* no markup typed in */
    CHECK(odg_spell_replace(&doc, &hit, "total") == OWF_OK);
    CHECK(odg_spell_next(&doc, spell, h, 0, &hit) == 1 && !strcmp(hit.word, "teh"));
    CHECK(odg_spell_replace(&doc, &hit, "the") == OWF_OK &&
          !strcmp(value_of(&doc, h), "<b>The</b> sales<br>total &amp; the"));
    /* a word the tags' names spell is not changed in them */
    CHECK(odg_set_value(&doc, h, "<bb>bb</bb>") == OWF_OK);
    CHECK(odg_spell_next(&doc, spell, h, 0, &hit) == 1 && !strcmp(hit.word, "bb"));
    CHECK(odg_spell_replace(&doc, &hit, "be") == OWF_OK && !strcmp(value_of(&doc, h), "<bb>be</bb>"));
    /* an entity inside the word: refused, the label as it was */
    CHECK(odg_set_value(&doc, h, "caf&#233; menu") == OWF_OK);
    CHECK(odg_spell_next(&doc, spell, h, 0, &hit) == 1 && !strcmp(hit.word, "caf\xc3\xa9"));
    CHECK(odg_spell_replace(&doc, &hit, "cafe") == OWF_ERR_UNSUPPORTED &&
          !strcmp(value_of(&doc, h), "caf&#233; menu"));
    CHECK(odg_set_value(&doc, h, "sales") == OWF_OK);

    /* ARexx SPELL */
    CHECK(odg_spell_rexx(&doc, spell, "", out, sizeof out) == 0 && !strcmp(out, "1"));   /* noo */
    CHECK(odg_set_value(&doc, a, "Hello wrold") == OWF_OK);
    CHECK(odg_spell_rexx(&doc, spell, "count", out, sizeof out) == 0 && !strcmp(out, "2"));
    {
        char want[96];
        snprintf(want, sizeof want, "%s wrold\n%s noo", a, e);
        CHECK(odg_spell_rexx(&doc, spell, "LIST", out, sizeof out) == 0 && !strcmp(out, want));
        CHECK(odg_spell_rexx(&doc, spell, "LIST", out, strlen(a) + 8) == 0 && !strncmp(out, a, strlen(a)) &&
              !strchr(out, '\n'));
    }
    CHECK(odg_spell_rexx(&doc, spell, "CHECK hello", out, sizeof out) == 0 && !strcmp(out, "1"));
    CHECK(odg_spell_rexx(&doc, spell, "IGNORE noo", out, sizeof out) == 0);
    CHECK(odg_spell_rexx(&doc, spell, "COUNT", out, sizeof out) == 0 && !strcmp(out, "1"));
    CHECK(odg_spell_rexx(&doc, spell, "SPIN", out, sizeof out) == 10);
    CHECK(odg_spell_rexx(&doc, NULL, "COUNT", out, sizeof out) == 5);
    CHECK(odg_spell_rexx(NULL, spell, "COUNT", out, sizeof out) == 10);
    CHECK(odg_spell_next(NULL, spell, NULL, 0, &hit) == -1 && odg_spell_replace(NULL, &hit, "x") == OWF_ERR_FORMAT);

    odg_free(&doc);
    owf_diagram_free(&d);
    owf_spell_close(spell);
    remove(path);
    if (fails) {
        printf("odg_spell: %d of %d checks failed\n", fails, checks);
        return 1;
    }
    printf("odg_spell: %d checks passed\n", checks);
    return 0;
}
