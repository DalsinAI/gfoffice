/*
 * test_opr_clip: slides from another deck and the sorter's slide clipboard
 * (src/deck/opr_clip.c):
 *
 *   - slides inserted from another deck at a place, one Undo step, Redo;
 *   - their pictures and fonts become the deck's, a master found by name or
 *     brought along (and gone again on Undo), names made free;
 *   - an effect follows its object's copy; kept XML objects left out and
 *     counted, a group's children kept with it;
 *   - the clipboard: copy, the source changed after, paste twice, paste into
 *     another deck; saved as ODP and read back;
 *   - refusals.
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

static const unsigned char png[] = { 0x89, 'P', 'N', 'G', 13, 10, 26, 10, 0, 0, 0, 0 };

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
    spec.look.fill = OWF_SHADE(0x3366CC);
    if (opr_obj_insert_shape(u, d, slide, &spec, &obj) < 0)
        return -1;
    return obj;
}

static const char *name_of(const owf_deck *d, int slide)
{
    return d->slides[slide].name ? d->slides[slide].name : "(none)";
}

/* src: slide 1 "Intro" on the default master; slide 2 "Pictures" on master "Dark" with a picture, a shape
 * in a font of its own that flies in, and a kept chart. */
static owf_deck *source(opr_undo *u, int *shape)
{
    owf_deck *d = owf_deck_new(OWF_DECK_16_9_WIDTH, OWF_DECK_16_9_HEIGHT);
    owf_master *dark;
    owf_obj *pic, *kept;
    owf_anim fly;
    int sel = 0, font, cls = 0, effect = 0;
    if (!d)
        return NULL;
    opr_slide_insert(u, d, 0, CONTENT, &sel);
    opr_slide_insert(u, d, 1, CONTENT, &sel);
    opr_slide_rename(u, d, 0, "Intro", &sel);
    opr_slide_rename(u, d, 1, "Pictures", &sel);
    dark = owf_deck_add_master(d, "Dark");
    if (!dark)
        return NULL;
    dark->background = OWF_SHADE(0x101030);
    dark->title = d->masters[0].title;
    d->slides[1].master = d->nmasters - 1;
    /* filler fonts first, so the shape's font index is not the same in both decks */
    owf_doc_font(d->res, "Filler One", OWF_FONT_SERIF);
    owf_doc_font(d->res, "Filler Two", OWF_FONT_SERIF);
    font = owf_doc_font(d->res, "Topaz", OWF_FONT_SANS);
    *shape = add_shape(u, d, 1, 1000, 1000);
    opr_text_set(d, 1, *shape, "Hello");
    if (*shape >= 0 && d->slides[1].objs[*shape].text.nparas && d->slides[1].objs[*shape].text.paras[0].nruns)
        d->slides[1].objs[*shape].text.paras[0].runs[0].fmt.font = font;
    pic = owf_slide_add(&d->slides[1], OWF_OBJ_PICTURE);
    if (pic) {
        pic->image = owf_doc_add_image(d->res, "dot.png", "image/png", png, sizeof png, 500, 500, "a dot");
        pic->x = 4000; pic->y = 1000; pic->width = 500; pic->height = 500;
    }
    kept = owf_slide_add(&d->slides[1], OWF_OBJ_SHAPE);
    if (kept) {
        const char *xml = "<draw:frame draw:name=\"chart\"/>";
        kept->keep = owf_deck_keep(d, OWF_KEPT_OBJECT, xml, strlen(xml));
        kept->x = 6000; kept->y = 1000; kept->width = 1000; kept->height = 1000;
    }
    memset(&fly, 0, sizeof fly);
    opr_effect_at(opr_effect_word("FLYIN"), &cls, &effect, NULL, NULL);
    fly.cls = (unsigned char)cls;
    fly.effect = (unsigned char)effect;
    fly.direction = OWF_DIR_LEFT;
    fly.duration = 700;
    opr_anim_add(u, d, 1, *shape, &fly, -1);
    opr_undo_clear(u);
    return d;
}

static int find_obj(const owf_slide *s, owf_obj_kind kind)
{
    int i;
    for (i = 0; i < s->nobjs; i++)
        if (s->objs[i].kind == kind)
            return i;
    return -1;
}

int main(void)
{
    opr_undo *su = opr_undo_new(), *u = opr_undo_new();
    owf_deck *src, *dst = owf_deck_new(OWF_DECK_16_9_WIDTH, OWF_DECK_16_9_HEIGHT), *back = NULL;
    opr_clip *clip = opr_clip_new();
    owf_report *report;
    int shape = -1, sel = 0, left = -1, slide = -1, obj = -1, masters, nimages, two[2] = { 0, 1 }, one[1] = { 1 };
    int k, p;

    if (!su || !u || !dst || !clip) {
        printf("FAIL no memory\n");
        return 1;
    }
    src = source(su, &shape);
    CHECK(src && src->nslides == 2 && shape >= 0, "the source deck");
    if (!src)
        return 1;
    for (k = 0; k < 2; k++)
        opr_slide_insert(u, dst, k, CONTENT, &sel);
    opr_slide_rename(u, dst, 0, "Intro", &sel);
    opr_undo_clear(u);
    masters = dst->nmasters;
    nimages = dst->res->nimages;

    /* both slides in after the first */
    CHECK(opr_slides_insert_from(u, dst, 1, src, two, 2, &sel, &left) == 0, "inserted");
    CHECK(dst->nslides == 4 && sel == 1, "four slides, the first new one picked: %d %d", dst->nslides, sel);
    CHECK(!strcmp(name_of(dst, 1), "Intro (2)"), "a taken name made free: %s", name_of(dst, 1));
    CHECK(!strcmp(name_of(dst, 2), "Pictures"), "the second's name: %s", name_of(dst, 2));
    CHECK(left == 1, "the kept chart left out: %d", left);
    CHECK(dst->nmasters == masters + 1, "master Dark came along: %d", dst->nmasters);
    CHECK(dst->slides[1].master == 0, "Intro (2) on the deck's own default master: %d", dst->slides[1].master);
    CHECK(dst->slides[2].master == masters && dst->masters[masters].name &&
          !strcmp(dst->masters[masters].name, "Dark"), "Pictures on Dark");
    CHECK(dst->masters[masters].background == OWF_SHADE(0x101030), "Dark's background");
    CHECK(dst->res->nimages == nimages + 1, "the picture added: %d", dst->res->nimages);
    p = find_obj(&dst->slides[2], OWF_OBJ_PICTURE);
    CHECK(p >= 0 && dst->slides[2].objs[p].image == nimages, "the picture object names the deck's picture");
    {
        const owf_slide *s = &dst->slides[2];
        int sh = find_obj(s, OWF_OBJ_SHAPE), f;
        CHECK(sh >= 0 && s->nobjs == src->slides[1].nobjs - 1, "objects but the kept one: %d", s->nobjs);
        if (sh >= 0 && s->objs[sh].text.nparas && s->objs[sh].text.paras[0].nruns) {
            f = s->objs[sh].text.paras[0].runs[0].fmt.font;
            CHECK(f >= 0 && f < dst->res->nfonts && !strcmp(dst->res->fonts[f].name, "Topaz"),
                  "the run's font is the deck's Topaz: %d", f);
        }
        CHECK(s->nanims == 1 && sh >= 0 && s->anims[0].obj == s->objs[sh].id && s->objs[sh].id > 0,
              "the effect names the copy");
        CHECK(s->keep_timing == -1, "no kept timing");
    }
    CHECK(opr_anim_steps(dst, 2) == 1, "the copy's effect plays: %d", opr_anim_steps(dst, 2));

    /* one Undo step, Redo */
    CHECK(opr_undo_undo(u, dst, &slide, &obj) == 0, "undo");
    CHECK(dst->nslides == 2 && dst->nmasters == masters, "undone: slides %d masters %d", dst->nslides, dst->nmasters);
    CHECK(opr_undo_redo(u, dst, &slide, &obj) == 0, "redo");
    CHECK(dst->nslides == 4 && dst->nmasters == masters + 1 && dst->slides[2].master == masters, "redone");

    /* again: Dark is found by name this time */
    CHECK(opr_slides_insert_from(u, dst, -1, src, one, 1, &sel, &left) == 0, "Pictures again at the end");
    CHECK(dst->nslides == 5 && sel == 4 && dst->nmasters == masters + 1, "no second Dark: %d", dst->nmasters);
    CHECK(!strcmp(name_of(dst, 4), "Pictures (2)"), "named %s", name_of(dst, 4));
    CHECK(dst->slides[4].master == masters, "on the same Dark");

    /* the clipboard */
    CHECK(opr_clip_count(clip) == 0, "an empty clipboard");
    CHECK(opr_clip_paste(clip, u, dst, 0, &sel) == -1, "nothing to paste");
    CHECK(opr_clip_copy(clip, src, two, 2) == 0 && opr_clip_count(clip) == 2, "two copied");
    opr_slide_rename(su, src, 0, "Changed", &sel);
    CHECK(opr_clip_paste(clip, u, dst, 0, &sel) == 0 && dst->nslides == 7, "pasted: %d", dst->nslides);
    CHECK(!strcmp(name_of(dst, 0), "Intro (3)"), "the copy as it was when copied: %s", name_of(dst, 0));
    CHECK(dst->nmasters == masters + 1, "Dark found again");
    CHECK(opr_clip_paste(clip, u, dst, -1, &sel) == 0 && dst->nslides == 9, "pasted twice");
    CHECK(opr_undo_undo(u, dst, &slide, &obj) == 0 && dst->nslides == 7, "the second paste undone");

    /* refusals */
    CHECK(opr_slides_insert_from(u, dst, 0, src, two, 0, &sel, &left) == -1, "no slides");
    k = 9;
    CHECK(opr_slides_insert_from(u, dst, 0, src, &k, 1, &sel, &left) == -1, "no such slide");
    CHECK(opr_slides_insert_from(u, dst, 0, dst, two, 1, &sel, &left) == -1, "not from itself");

    /* saved and read back */
    report = owf_report_new();
    CHECK(owf_export_deck_file(dst, "build/test-opr-clip.odp", "odp", report) == 0, "saved as ODP");
    CHECK(owf_import_deck_file("build/test-opr-clip.odp", "odp", &back, report, NULL) == 0 && back, "read back");
    if (back) {
        int found = 0;
        CHECK(back->nslides == dst->nslides, "slides %d %d", back->nslides, dst->nslides);
        for (k = 0; k < back->nslides; k++)
            if (back->slides[k].name && !strcmp(back->slides[k].name, "Pictures") &&
                find_obj(&back->slides[k], OWF_OBJ_PICTURE) >= 0 && back->slides[k].nanims == 1)
                found = 1;
        CHECK(found, "Pictures read back with its picture and its effect");
        owf_deck_free(back);
    }
    owf_report_free(report);
    remove("build/test-opr-clip.odp");

    opr_clip_free(clip);
    owf_deck_free(src);
    owf_deck_free(dst);
    opr_undo_free(su);
    opr_undo_free(u);
    printf("test_opr_clip: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
