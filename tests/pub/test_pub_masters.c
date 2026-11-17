/*
 * test_pub_masters: GFOpenPressmaster's master pages on a publication
 * (the shared Master view, src/deck/opr_master.c) and Insert > Page Number
 * (src/pub/opp_pagenum.c, the field in src/pub/opp_merge.c):
 *
 *   - the Newsletter template's pages share one master; the Master view has
 *     a slide for it;
 *   - a page number box put on the master in the view, written back as one
 *     Undo step: every page lays it out from the master; Undo and Redo;
 *   - a page number box on one page only, and the number of pages;
 *   - the field inserted inside text, between two halves of a run;
 *   - a second master, Page Design giving it to page 2: page 2 no longer
 *     shows the first master's number;
 *   - the masters saved as ODP and read back with the page number field.
 *
 * Prints what fails; exit 1 when anything did.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "openpressmaster_pagenum.h"
#include "openpressmaster_merge.h"
#include "openpressmaster_template.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures, checks;

#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
    printf(__VA_ARGS__); printf("\n"); } } while (0)

/* the field run kind an object's first paragraph has, or -1 */
static int field_in(const owf_obj *o)
{
    int j;
    if (!o || !o->text.nparas) return -1;
    for (j = 0; j < o->text.paras[0].nruns; j++)
        if (o->text.paras[0].runs[j].kind == OWF_RUN_FIELD) return (int)o->text.paras[0].runs[j].field;
    return -1;
}

/* whether page slide lays out master object mobj of its master */
static int shows_master_obj(const owf_deck *d, int slide, const owf_obj *mobj)
{
    opr_slide *s = opr_slide_layout(d, slide, NULL, NULL, "1 January 2026");
    int i, found = 0;
    if (!s) return -1;
    for (i = 0; i < s->nframes; i++)
        if (s->frames[i].from_master && s->frames[i].obj == mobj) found = 1;
    opr_slide_free(s);
    return found;
}

int main(void)
{
    owf_deck *d = NULL, *w, *back = NULL;
    opr_undo *u = opr_undo_new(), *vu = opr_undo_new();
    opr_master_view *v;
    owf_report *report;
    int t, i, sel = -1, slide = -1, obj = -1, before, mobj;

    t = opp_template_find("Newsletter");
    CHECK(t >= 0 && opp_template_new(t, &d) == 0 && d, "Newsletter made");
    if (!d || !u || !vu) {
        printf("FAIL no publication\n");
        return 1;
    }
    CHECK(d->nslides == 2 && d->nmasters >= 1, "pages %d masters %d", d->nslides, d->nmasters);
    CHECK(d->slides[0].master == 0 && d->slides[1].master == 0, "both pages on master 1");

    /* the Master view: a page number on the master */
    v = opr_master_view_open(d);
    CHECK(v != NULL, "Master view opened");
    if (!v) return 1;
    w = opr_master_view_deck(v);
    CHECK(w->nslides == d->nmasters, "a slide a master: %d %d", w->nslides, d->nmasters);
    before = d->masters[0].nobjs;
    CHECK(opp_page_number_box(vu, w, 0, OWF_FIELD_PAGE, &sel) == 0, "page number box on the master");
    CHECK(sel == w->slides[0].nobjs - 1, "the box selected: %d", sel);
    if (sel >= 0) {
        const owf_obj *o = &w->slides[0].objs[sel];
        CHECK(field_in(o) == OWF_FIELD_PAGE, "its field is the page number: %d", field_in(o));
        CHECK(o->x + o->width / 2 == w->width / 2 || o->x + o->width / 2 == w->width / 2 - 1, "centred: %d %d",
              o->x, o->width);
        CHECK(o->y + o->height < w->height && o->y > w->height / 2, "near the foot: %d", o->y);
        CHECK(o->text.paras[0].fmt.align == OWF_ALIGN_CENTRE, "its text centred");
    }
    CHECK(opr_master_view_commit(v, u) == 0, "written back");
    CHECK(d->masters[0].nobjs == before + 1, "the master has the box: %d", d->masters[0].nobjs);
    mobj = d->masters[0].nobjs - 1;
    if (mobj >= 0) {
        CHECK(field_in(&d->masters[0].objs[mobj]) == OWF_FIELD_PAGE, "the master's field");
        for (i = 0; i < d->nslides; i++)
            CHECK(shows_master_obj(d, i, &d->masters[0].objs[mobj]) == 1, "page %d shows the master's number", i + 1);
    }

    opr_master_view_free(v);
    v = NULL;
    opr_undo_free(vu);
    vu = opr_undo_new();

    /* one Undo step of the publication, and Redo */
    CHECK(opr_undo_undo(u, d, &slide, &obj) == 0 && d->masters[0].nobjs == before, "undone: %d", d->masters[0].nobjs);
    CHECK(opr_undo_redo(u, d, &slide, &obj) == 0 && d->masters[0].nobjs == before + 1, "redone");
    CHECK(field_in(&d->masters[0].objs[d->masters[0].nobjs - 1]) == OWF_FIELD_PAGE, "redo brings the field back");

    /* on one page only, and the number of pages; one Undo step each */
    before = d->slides[1].nobjs;
    CHECK(opp_page_number_box(u, d, 1, OWF_FIELD_PAGES, &sel) == 0, "number of pages on page 2");
    CHECK(d->slides[1].nobjs == before + 1 && field_in(&d->slides[1].objs[before]) == OWF_FIELD_PAGES,
          "page 2's box holds the number of pages");
    CHECK(opr_undo_undo(u, d, &slide, &obj) == 0 && d->slides[1].nobjs == before, "the box and its field one step");
    CHECK(opr_undo_redo(u, d, &slide, &obj) == 0 && field_in(&d->slides[1].objs[before]) == OWF_FIELD_PAGES,
          "redone with its field");
    CHECK(opp_page_number_box(u, d, 1, OWF_FIELD_DATE, &sel) == -1, "another field refused");
    CHECK(opp_page_number_box(u, d, 7, OWF_FIELD_PAGE, &sel) == -1, "no such page refused");

    /* inside text: "Page " + field + " of " + pages */
    {
        opr_textbox_spec spec;
        owf_obj *o;
        memset(&spec, 0, sizeof spec);
        spec.x = spec.y = 1000;
        spec.width = 3000;
        spec.height = 600;
        spec.text = "Page  of ";
        CHECK(opr_obj_insert_textbox(u, d, 0, &spec, &sel) == 0, "a text box");
        o = &d->slides[0].objs[sel];
        CHECK(opp_insert_page_field_at(o, 0, 5, OWF_FIELD_PAGE) == OWF_OK, "page number after \"Page \"");
        CHECK(opp_insert_page_field_at(o, 0, 9, OWF_FIELD_PAGES) == OWF_OK, "pages at the end");
        CHECK(o->text.paras[0].nruns == 4, "text, field, text, field: %d runs", o->text.paras[0].nruns);
        if (o->text.paras[0].nruns == 4) {
            CHECK(!strcmp(o->text.paras[0].runs[0].text, "Page "), "first half: %s", o->text.paras[0].runs[0].text);
            CHECK(o->text.paras[0].runs[1].field == OWF_FIELD_PAGE, "the page field");
            CHECK(!strcmp(o->text.paras[0].runs[2].text, " of "), "second half: %s", o->text.paras[0].runs[2].text);
            CHECK(o->text.paras[0].runs[3].field == OWF_FIELD_PAGES, "the pages field");
        }
        CHECK(opp_insert_page_field_at(o, 3, 0, OWF_FIELD_PAGE) == OWF_ERR_FORMAT, "no such paragraph refused");
        CHECK(opp_insert_page_field_at(o, 0, 0, OWF_FIELD_MERGE) == OWF_ERR_FORMAT, "a merge field is not a page field");
    }

    /* a second master; Page Design gives it to page 2 */
    v = opr_master_view_open(d);
    CHECK(v != NULL, "the view again");
    if (v) {
        w = opr_master_view_deck(v);
        CHECK(opr_slide_insert(vu, w, w->nslides, -1, &sel) == 0, "a new master");
        CHECK(opr_master_view_commit(v, u) == 0 && d->nmasters == 2, "two masters: %d", d->nmasters);
        opr_master_view_free(v);
        v = NULL;
    }
    mobj = d->masters[0].nobjs - 1;
    CHECK(opr_slide_set_master(u, d, 1, 1) == 0 && d->slides[1].master == 1, "Page Design: page 2 on master 2");
    CHECK(shows_master_obj(d, 0, &d->masters[0].objs[mobj]) == 1, "page 1 still shows master 1's number");
    CHECK(shows_master_obj(d, 1, &d->masters[0].objs[mobj]) == 0, "page 2 no longer does");
    CHECK(opr_undo_undo(u, d, &slide, &obj) == 0 && d->slides[1].master == 0, "Page Design undone");

    /* saved as ODP and read back: the master's page number field */
    report = owf_report_new();
    CHECK(owf_export_deck_file(d, "build/test-pub-masters.odp", "odp", report) == 0, "saved as ODP");
    CHECK(owf_import_deck_file("build/test-pub-masters.odp", "odp", &back, report, NULL) == 0 && back, "read back");
    if (back) {
        int k, found = 0;
        CHECK(back->nmasters == d->nmasters, "masters %d %d", back->nmasters, d->nmasters);
        for (k = 0; k < back->masters[0].nobjs; k++)
            if (field_in(&back->masters[0].objs[k]) == OWF_FIELD_PAGE) found = 1;
        CHECK(found, "master 1's page number field read back");
        CHECK(back->nslides == 2 && back->slides[0].master == 0, "pages and their masters");
        owf_deck_free(back);
    }
    owf_report_free(report);
    remove("build/test-pub-masters.odp");

    owf_deck_free(d);
    opr_undo_free(u);
    opr_undo_free(vu);
    printf("test_pub_masters: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
