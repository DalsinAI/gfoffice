/* Merge fields shown and saved by the page model: the layout draws the value a field shows, ODP keeps the field
 * (text:database-display) and PPTX the value as text. MIT, Copyright (c) 2026 AmiCradle Team. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "openpresentation_core.h"
#include "openpublisher_merge.h"

static int checks, failed;

#define CHECK(c) do { ++checks; if (!(c)) { ++failed; fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static ow_merge_table *table(const char *text)
{
    int memory = 0;
    return ow_merge_table_from_text(text, strlen(text), &memory);
}

/* One page, one text box holding "To " and a merge field for Name */
static owf_deck *card(void)
{
    owf_deck *d = owf_deck_new(11906, 16838);
    owf_slide *slide;
    owf_obj *o;
    owf_parafmt pf;
    owf_charfmt cf;
    owf_para *p;
    if (!d || !(slide = owf_deck_add_slide(d, -1, -1)) || !(o = owf_slide_add(slide, OWF_OBJ_TEXT))) goto bad;
    o->x = 1000; o->y = 1000; o->width = 9000; o->height = 2000;
    owf_parafmt_init(&pf);
    owf_charfmt_init(&cf);
    if (!(p = owf_story_add(&o->text, &pf)) || owf_para_add_text(p, &cf, "To ", 3) != OWF_OK) goto bad;
    if (opp_merge_insert_field(o, -1, "Name", NULL) != OWF_OK) goto bad;
    return d;
bad:
    owf_deck_free(d);
    return NULL;
}

/* How wide the box's drawn text is */
static long drawn_width(const owf_deck *d)
{
    opr_slide *s = opr_slide_layout(d, 0, NULL, NULL, "1.1.2026");
    long left = 0x7FFFFFFFL, right = 0;
    size_t i;
    int frame;
    if (!s) return -1;
    frame = opr_slide_frame_for(s, 0);
    for (i = 0; i < ol_display_count(s->doc); ++i) {
        const ol_display_op *op = ol_display_get(s->doc, i);
        if (op->type != OL_OP_TEXT || opr_slide_frame_of(s, op) != frame) continue;
        if (op->u.text.bounds.x < left) left = op->u.text.bounds.x;
        if (op->u.text.bounds.x + op->u.text.bounds.width > right) right = op->u.text.bounds.x + op->u.text.bounds.width;
    }
    opr_slide_free(s);
    return right > left ? right - left : 0;
}

static const owf_run *field_of(const owf_deck *d)
{
    int s, o, p, r;
    for (s = 0; s < d->nslides; ++s)
        for (o = 0; o < d->slides[s].nobjs; ++o)
            for (p = 0; p < d->slides[s].objs[o].text.nparas; ++p)
                for (r = 0; r < d->slides[s].objs[o].text.paras[p].nruns; ++r) {
                    const owf_run *run = &d->slides[s].objs[o].text.paras[p].runs[r];
                    if (run->kind == OWF_RUN_FIELD) return run;
                }
    return NULL;
}

static int has(const unsigned char *data, size_t length, const char *what)
{
    size_t n = strlen(what), i;
    for (i = 0; i + n <= length; ++i)
        if (!memcmp(data + i, what, n)) return 1;
    return 0;
}

static void layout(void)
{
    owf_deck *d = card();
    ow_merge_table *t = table("Name\nWilhelmina Montgomery-Smythe\nIo\n");
    long longer, shorter;
    CHECK(d && t);
    if (!d || !t) { owf_deck_free(d); ow_merge_table_free(t); return; }
    CHECK(opp_merge_show(d, t, 0, NULL) == OWF_OK);
    longer = drawn_width(d);
    CHECK(opp_merge_show(d, t, 1, NULL) == OWF_OK);
    shorter = drawn_width(d);
    CHECK(longer > 0 && shorter > 0);
    CHECK(longer > shorter * 3);                     /* the value is drawn, not a page number */
    ow_merge_table_free(t);
    owf_deck_free(d);
}

static void odp(void)
{
    owf_deck *d = card(), *back = NULL;
    ow_merge_table *t = table("Name\n\"Jo & \"\"Al\"\" <x>\"\n");
    const owf_deck_format *f = owf_deck_format_named("odp");
    unsigned char *data = NULL;
    size_t length = 0;
    const owf_run *r;
    CHECK(d && t && f);
    if (!d || !t || !f) { owf_deck_free(d); ow_merge_table_free(t); return; }
    CHECK(opp_merge_show(d, t, 0, NULL) == OWF_OK);
    CHECK(f->export(d, &data, &length, NULL) == OWF_OK);
    CHECK(owf_import_deck_memory(data, length, "odp", &back, NULL, NULL) == OWF_OK && back);
    r = back ? field_of(back) : NULL;
    CHECK(r && r->field == OWF_FIELD_MERGE);
    CHECK(r && r->href && !strcmp(r->href, "Name"));
    CHECK(r && r->text && !strcmp(r->text, "Jo & \"Al\" <x>"));
    if (r && r->text && strcmp(r->text, "Jo & \"Al\" <x>")) fprintf(stderr, "  read back \"%s\"\n", r->text);
    CHECK(back && opp_merge_fields(back, t, NULL) == 1);
    free(data);
    owf_deck_free(back);
    ow_merge_table_free(t);
    owf_deck_free(d);
}

static void pptx(void)
{
    owf_deck *d = card();
    ow_merge_table *t = table("Name\nZebedee\n");
    unsigned char *data = NULL;
    size_t length = 0;
    owf_deck *back = NULL;
    CHECK(d && t);
    if (!d || !t) { owf_deck_free(d); ow_merge_table_free(t); return; }
    CHECK(opp_merge_show(d, t, 0, NULL) == OWF_OK);
    CHECK(opr_export_pptx(d, &data, &length, NULL) == OWF_OK && length);
    CHECK(owf_import_deck_memory(data, length, "pptx", &back, NULL, NULL) == OWF_OK && back);
    if (back) {                                      /* PowerPoint has no merge field: the value as text */
        const owf_obj *o = &back->slides[0].objs[back->slides[0].nobjs - 1];
        int p, r, found = 0;
        for (p = 0; p < o->text.nparas; ++p)
            for (r = 0; r < o->text.paras[p].nruns; ++r)
                if (o->text.paras[p].runs[r].text && strstr(o->text.paras[p].runs[r].text, "Zebedee")) found = 1;
        CHECK(found);
    }
    (void)has;
    free(data);
    owf_deck_free(back);
    ow_merge_table_free(t);
    owf_deck_free(d);
}

int main(void)
{
    layout();
    odp();
    pptx();
    if (failed) { fprintf(stderr, "publisher merge fields: %d of %d checks failed\n", failed, checks); return 1; }
    printf("publisher merge fields shown and saved (%d checks)\n", checks);
    return 0;
}
