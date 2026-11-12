/*
 * test_opr_master: the Master view (src/deck/opr_master.c, the masters' Undo
 * steps in src/deck/opr_edit.c):
 *
 *   - the view has a slide for each master, with its objects and background;
 *   - art added, moved and deleted there, a master renamed, added and
 *     deleted, written back as one Undo step of the deck, Undo and Redo;
 *   - the deck's slides follow their master when the masters move, and a
 *     deleted master's slides go to the first;
 *   - nothing changed: nothing written; the last master is never deleted;
 *   - a slide's master set (Slide Design), one Undo step;
 *   - the masters saved as ODP and read back with their art;
 *   - what the view borrowed (pictures, kept XML) given back.
 *
 * Prints what fails; exit 1 when anything did.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "openpresentation_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures, checks;

#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
    printf(__VA_ARGS__); printf("\n"); } } while (0)

#define CONTENT 2                /* owf_deck_new's "Title, Content" */

static int add_shape(opr_undo *u, owf_deck *d, int slide, int x, int y)
{
    opr_shape_spec spec;
    int obj = -1;
    memset(&spec, 0, sizeof spec);
    spec.shape = OWF_SHAPE_RECT;
    spec.x = x;
    spec.y = y;
    spec.width = 1000;
    spec.height = 800;
    spec.look.fill = OWF_SHADE(0xCC3366);
    if (opr_obj_insert_shape(u, d, slide, &spec, &obj) < 0)
        return -1;
    return obj;
}

static const char *name_of(const owf_master *m)
{
    return m->name ? m->name : "(none)";
}

int main(void)
{
    opr_undo *u = opr_undo_new(), *vu = opr_undo_new();
    owf_deck *d = owf_deck_new(OWF_DECK_16_9_WIDTH, OWF_DECK_16_9_HEIGHT), *w, *back = NULL;
    opr_master_view *v;
    int sel = 0, i, slide = -1, obj = -1, art, before_objs, nkept;
    owf_master *m2;
    owf_report *report;

    if (!u || !vu || !d) {
        printf("FAIL no memory\n");
        return 1;
    }
    /* three slides on one master, then a second master made by hand */
    for (i = 0; i < 3; i++)
        opr_slide_insert(u, d, i, CONTENT, &sel);
    CHECK(d->nslides == 3 && d->nmasters >= 1, "slides %d masters %d", d->nslides, d->nmasters);
    m2 = owf_deck_add_master(d, "Dark");
    CHECK(m2 != NULL, "second master");
    if (m2) {
        m2->background = OWF_SHADE(0x202040);
        m2->title = d->masters[0].title;
    }
    d->slides[2].master = d->nmasters - 1;
    opr_undo_clear(u);
    before_objs = d->masters[0].nobjs;
    nkept = d->nkept;

    /* the view */
    v = opr_master_view_open(d);
    CHECK(v != NULL, "view opened");
    if (!v)
        return 1;
    w = opr_master_view_deck(v);
    CHECK(w->nslides == d->nmasters, "a slide a master: %d %d", w->nslides, d->nmasters);
    CHECK(w->slides[0].nobjs == d->masters[0].nobjs, "master 0's objects: %d %d", w->slides[0].nobjs, d->masters[0].nobjs);
    CHECK(w->slides[1].background == OWF_SHADE(0x202040), "master 1's background");
    CHECK(w->slides[1].name && !strcmp(w->slides[1].name, "Dark"), "master 1's name as the slide's");
    CHECK(w->res == d->res, "pictures and fonts shared");
    CHECK(opr_master_view_origin(v, 1) == 1, "slide 1 was master 1");

    /* nothing changed: nothing to write */
    CHECK(!opr_undo_can(vu), "no steps yet");

    /* art on master 0 and a renamed master 1, as the view's own steps */
    art = add_shape(vu, w, 0, 500, 500);
    CHECK(art >= 0 && w->slides[0].nobjs == before_objs + 1, "art added in the view");
    CHECK(opr_obj_set_box(vu, w, 0, art, 600, 700, 1200, 900) == 0, "art moved");
    CHECK(opr_slide_rename(vu, w, 1, "Night", &sel) == 0, "master renamed");
    CHECK(opr_undo_can(vu), "the view has steps");
    CHECK(d->masters[0].nobjs == before_objs, "the deck unchanged until written back");

    CHECK(opr_master_view_commit(v, u) == 0, "written back");
    CHECK(d->masters[0].nobjs == before_objs + 1, "the art on master 0: %d", d->masters[0].nobjs);
    if (d->masters[0].nobjs == before_objs + 1) {
        const owf_obj *o = &d->masters[0].objs[before_objs];
        CHECK(o->x == 600 && o->y == 700 && o->width == 1200, "the art where it was moved: %d %d %d", o->x, o->y, o->width);
    }
    CHECK(!strcmp(name_of(&d->masters[1]), "Night"), "renamed: %s", name_of(&d->masters[1]));
    CHECK(d->masters[1].background == OWF_SHADE(0x202040), "background kept");
    CHECK(d->slides[2].master == 1 && d->slides[0].master == 0, "slides keep their masters");

    /* one Undo step of the deck, and Redo */
    CHECK(opr_undo_undo(u, d, &slide, &obj) == 0, "undo");
    CHECK(d->masters[0].nobjs == before_objs && !strcmp(name_of(&d->masters[1]), "Dark"), "undone");
    CHECK(opr_undo_redo(u, d, &slide, &obj) == 0, "redo");
    CHECK(d->masters[0].nobjs == before_objs + 1 && !strcmp(name_of(&d->masters[1]), "Night"), "redone");

    /* masters moved: master 1 first; a new master; slides follow */
    CHECK(opr_slide_move(vu, w, 1, 0, &sel) == 0, "master moved up");
    CHECK(opr_slide_insert(vu, w, 2, -1, &sel) == 0, "a new master");
    CHECK(w->nslides == 3, "three in the view");
    CHECK(opr_master_view_origin(v, 2) == -1, "the new one is new");
    CHECK(opr_master_view_commit(v, u) == 0, "written back again");
    CHECK(d->nmasters == 3, "three masters: %d", d->nmasters);
    CHECK(!strcmp(name_of(&d->masters[0]), "Night"), "Night first: %s", name_of(&d->masters[0]));
    CHECK(d->slides[2].master == 0 && d->slides[0].master == 1 && d->slides[1].master == 1,
          "slides followed: %d %d %d", d->slides[0].master, d->slides[1].master, d->slides[2].master);
    CHECK(opr_master_view_origin(v, 2) == 2, "after writing back each slide is its master");

    /* a master deleted: its slides go to the first */
    CHECK(opr_slide_delete(vu, w, 0, &sel) == 0, "Night deleted");
    CHECK(opr_master_view_commit(v, u) == 0, "written back a third time");
    CHECK(d->nmasters == 2, "two masters: %d", d->nmasters);
    CHECK(d->slides[2].master == 0 && d->slides[0].master == 0, "Night's slide on the first master: %d", d->slides[2].master);
    CHECK(opr_undo_undo(u, d, &slide, &obj) == 0 && d->nmasters == 3 && d->slides[2].master == 0 &&
          d->slides[0].master == 1, "the delete undone");
    CHECK(opr_undo_redo(u, d, &slide, &obj) == 0 && d->nmasters == 2, "and redone");

    /* the last master stays */
    while (w->nslides > 1)
        opr_slide_delete(vu, w, 0, &sel);
    if (w->nslides == 1) {
        owf_slide gone = w->slides[0];
        w->nslides = 0;
        CHECK(opr_master_view_commit(v, u) == -2, "no masters refused");
        w->nslides = 1;
        w->slides[0] = gone;
    }

    opr_master_view_free(v);
    CHECK(d->res != NULL && d->nkept == nkept, "borrowed parts given back");

    /* Slide Design: one slide's master, one Undo step */
    CHECK(opr_slide_set_master(u, d, 1, 1) == 0 && d->slides[1].master == 1, "slide 2 on master 2");
    CHECK(opr_undo_undo(u, d, &slide, &obj) == 0 && d->slides[1].master == 0, "Slide Design undone");
    CHECK(opr_slide_set_master(u, d, 1, 9) == -1, "no such master refused");
    CHECK(opr_master_slides(d, 0) == 3, "master 1 has three slides: %d", opr_master_slides(d, 0));

    /* saved and read back: the art and the names */
    report = owf_report_new();
    CHECK(owf_export_deck_file(d, "build/test-opr-master.odp", "odp", report) == 0, "saved as ODP");
    CHECK(owf_import_deck_file("build/test-opr-master.odp", "odp", &back, report, NULL) == 0 && back, "read back");
    if (back) {
        int k, found = 0;
        CHECK(back->nmasters == d->nmasters, "masters %d %d", back->nmasters, d->nmasters);
        for (k = 0; k < back->nmasters; k++) {
            int j;
            for (j = 0; j < back->masters[k].nobjs; j++) {
                const owf_obj *o = &back->masters[k].objs[j];
                if (o->kind == OWF_OBJ_SHAPE && o->x == 600 && o->y == 700)
                    found = 1;
            }
        }
        CHECK(found, "the master's art read back");
        owf_deck_free(back);
    }
    owf_report_free(report);
    remove("build/test-opr-master.odp");

    owf_deck_free(d);
    opr_undo_free(u);
    opr_undo_free(vu);
    printf("test_opr_master: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
