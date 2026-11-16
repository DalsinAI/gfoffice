/* The Area window's core (Presentation gap 1): opr_area_get/opr_area_put turn an object's transparency,
 * hatching and shadow into the window's values and back, and opr_obj_set_look and opr_slide_set_background
 * make them one undo step each. */

#include "openpresentation_core.h"

#include <stdio.h>
#include <string.h>

static int failures, checks;

#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
    printf(__VA_ARGS__); printf("\n"); } } while (0)

static void defaults(void)
{
    owf_look look;
    opr_area a;
    memset(&look, 0, sizeof look);
    look.fill = OWF_SHADE(0x336699);
    opr_area_get(&look, &a);
    CHECK(a.transparency == 0 && a.hatch == 0 && a.shadow == 0, "a plain fill has no extras");
    CHECK(a.hatch_spacing == OWF_HATCH_DISTANCE && a.hatch_degrees == 0 && a.hatch_colour == 0,
          "hatching shows its defaults");
    CHECK(a.shadow_colour == 0x808080 && a.shadow_dx == OPR_AREA_SHADOW_OFFSET &&
          a.shadow_dy == OPR_AREA_SHADOW_OFFSET && a.shadow_transparency == 0, "a shadow shows its defaults");
    CHECK(!opr_area_put(&look, &a), "putting the defaults back is accepted");
    {
        owf_look plain;
        memset(&plain, 0, sizeof plain);
        plain.fill = OWF_SHADE(0x336699);
        CHECK(!memcmp(&look, &plain, sizeof look), "the defaults change nothing");
    }
}

static void put_and_get(void)
{
    owf_look look, before;
    opr_area a, b;
    memset(&look, 0, sizeof look);
    look.fill = OWF_SHADE(0xffffff);
    look.line = OWF_SHADE(0x000000);
    look.line_width = 20;
    opr_area_get(&look, &a);
    a.transparency = 40;
    a.hatch = OPR_HATCH_DOUBLE;
    a.hatch_colour = 0x112233;
    a.hatch_degrees = 30;
    a.hatch_spacing = 200;
    a.shadow = 1;
    a.shadow_colour = 0x445566;
    a.shadow_dx = 50;
    a.shadow_dy = -60;
    a.shadow_transparency = 25;
    CHECK(!opr_area_put(&look, &a), "a full area is accepted");
    CHECK(look.transparency == 40 && look.hatch == OWF_HATCH_DOUBLE && look.hatch_colour == OWF_SHADE(0x112233) &&
          look.hatch_angle == 300 && look.hatch_distance == 200, "hatching and transparency set");
    CHECK(look.shadow == 1 && look.shadow_colour == OWF_SHADE(0x445566) && look.shadow_dx == 50 &&
          look.shadow_dy == -60 && look.shadow_transparency == 25, "the shadow set");
    CHECK(look.fill == OWF_SHADE(0xffffff) && look.line_width == 20, "the fill and line stay");
    opr_area_get(&look, &b);
    CHECK(!memcmp(&a, &b, sizeof a), "get gives back what was put");

    /* values stored more finely than the window shows survive an unchanged Apply */
    look.hatch_angle = 455;
    look.hatch_distance = 0;
    look.hatch_colour = 0;
    look.shadow_colour = 0;
    before = look;
    opr_area_get(&look, &a);
    CHECK(a.hatch_degrees == 45 && a.hatch_spacing == OWF_HATCH_DISTANCE && a.hatch_colour == 0 &&
          a.shadow_colour == 0x808080, "stored defaults shown");
    CHECK(!opr_area_put(&look, &a) && !memcmp(&look, &before, sizeof look), "an unchanged Apply keeps them");
    a.hatch_degrees = 90;
    CHECK(!opr_area_put(&look, &a) && look.hatch_angle == 900 && look.hatch_distance == 0,
          "a new angle replaces the finer one only");
    look.hatch_angle = -900;
    opr_area_get(&look, &a);
    CHECK(a.hatch_degrees == 270, "a negative angle shows as its turn");

    /* turning things off clears their fields */
    a.hatch = 0;
    a.shadow = 0;
    CHECK(!opr_area_put(&look, &a), "no hatch and no shadow accepted");
    CHECK(look.hatch == 0 && look.hatch_angle == 0 && look.hatch_distance == 0 && look.hatch_colour == 0 &&
          look.shadow == 0 && look.shadow_dx == 0 && look.shadow_dy == 0 && look.shadow_colour == 0 &&
          look.shadow_transparency == 0 && look.transparency == 40, "their fields cleared, transparency kept");
}

static void refused(void)
{
    owf_look look, before;
    opr_area a, bad;
    memset(&look, 0, sizeof look);
    look.fill = OWF_SHADE(0x00ff00);
    before = look;
    opr_area_get(&look, &a);
    a.hatch = OPR_HATCH_SINGLE;
    bad = a; bad.transparency = 101;
    CHECK(opr_area_put(&look, &bad) == -2, "transparency over 100 refused");
    bad = a; bad.transparency = -1;
    CHECK(opr_area_put(&look, &bad) == -2, "negative transparency refused");
    bad = a; bad.hatch = 4;
    CHECK(opr_area_put(&look, &bad) == -2, "an unknown hatch refused");
    bad = a; bad.hatch_spacing = 9;
    CHECK(opr_area_put(&look, &bad) == -2, "too fine a hatch refused");
    bad = a; bad.hatch_degrees = 360;
    CHECK(opr_area_put(&look, &bad) == -2, "360 degrees refused");
    bad = a; bad.hatch_colour = 0x1000000UL;
    CHECK(opr_area_put(&look, &bad) == -2, "a colour over 24 bits refused");
    bad = a; bad.shadow = 1; bad.shadow_dx = 10001;
    CHECK(opr_area_put(&look, &bad) == -2, "too far a shadow refused");
    bad = a; bad.shadow = 1; bad.shadow_transparency = 101;
    CHECK(opr_area_put(&look, &bad) == -2, "a shadow over 100 percent see-through refused");
    CHECK(!memcmp(&look, &before, sizeof look), "a refused area leaves the look alone");
}

static void undo_steps(void)
{
    owf_deck *d = owf_deck_new(10000, 8000);
    owf_slide *s = d ? owf_deck_add_slide(d, -1, -1) : NULL;
    opr_undo *u = opr_undo_new();
    owf_obj *o = s ? owf_slide_add(s, OWF_OBJ_SHAPE) : NULL;
    owf_look look, bg, got, bad;
    opr_area a;
    int slide = -1, obj = -1;
    CHECK(d && s && u && o, "a deck with a shape");
    if (!d || !s || !u || !o) {
        opr_undo_free(u);
        owf_deck_free(d);
        return;
    }
    o->shape = OWF_SHAPE_RECT;
    o->x = 100; o->y = 100; o->width = 2000; o->height = 1000;
    o->look.fill = OWF_SHADE(0xff0000);
    look = o->look;
    opr_area_get(&look, &a);
    a.transparency = 50; a.hatch = OPR_HATCH_TRIPLE; a.hatch_colour = 0x0000ff; a.hatch_degrees = 45;
    a.hatch_spacing = 150; a.shadow = 1;
    CHECK(!opr_area_put(&look, &a), "an area for the shape");
    CHECK(!opr_obj_set_look(u, d, 0, 0, &look) && !memcmp(&o->look, &look, sizeof look),
          "the shape takes hatching, transparency and a shadow");
    CHECK(!opr_undo_undo(u, d, &slide, &obj) && slide == 0 && obj == 0 && o->look.hatch == 0 &&
          o->look.transparency == 0 && o->look.shadow == 0 && o->look.fill == OWF_SHADE(0xff0000),
          "undo takes them away");
    CHECK(!opr_undo_redo(u, d, &slide, &obj) && !memcmp(&o->look, &look, sizeof look), "redo brings them back");
    bad = look; bad.transparency = 101;
    CHECK(opr_obj_set_look(u, d, 0, 0, &bad) == -2, "a look over 100 percent see-through refused");
    bad = look; bad.hatch = 9;
    CHECK(opr_obj_set_look(u, d, 0, 0, &bad) == -2, "a look with an unknown hatch refused");
    bad = look; bad.shadow_dx = 30000;
    CHECK(opr_obj_set_look(u, d, 0, 0, &bad) == -2, "a look with too far a shadow refused");

    opr_undo_clear(u);
    memset(&bg, 0, sizeof bg);
    bg.fill = OWF_SHADE(0xeeeeee);
    bg.transparency = 30;
    bg.hatch = OWF_HATCH_SINGLE;
    bg.hatch_colour = OWF_SHADE(0x993300);
    bg.hatch_angle = 450;
    bg.hatch_distance = 120;
    bg.line = OWF_SHADE(0x123456);   /* not a background field: ignored */
    bg.shadow = 1;
    CHECK(!opr_slide_set_background(u, d, 0, &bg), "the slide takes a hatched see-through background");
    OWF_BACKGROUND_LOOK(&d->slides[0], &got);
    CHECK(got.fill == OWF_SHADE(0xeeeeee) && got.transparency == 30 && got.hatch == OWF_HATCH_SINGLE &&
          got.hatch_colour == OWF_SHADE(0x993300) && got.hatch_angle == 450 && got.hatch_distance == 120,
          "the background fields set");
    CHECK(!opr_undo_undo(u, d, &slide, &obj) && slide == 0 && obj == -1 && !d->slides[0].background &&
          !d->slides[0].background_hatch && !d->slides[0].background_transparency, "undo the background");
    CHECK(!opr_undo_redo(u, d, &slide, &obj) && d->slides[0].background_hatch == OWF_HATCH_SINGLE &&
          d->slides[0].background_hatch_distance == 120, "redo the background");
    opr_undo_clear(u);
    CHECK(!opr_slide_set_background(u, d, 0, &bg) && !opr_undo_can(u), "the same background is not an edit");
    bad = bg; bad.hatch = 7;
    CHECK(opr_slide_set_background(u, d, 0, &bad) == -2, "a background with an unknown hatch refused");
    bad = bg; bad.fill = 0x2000000UL;
    CHECK(opr_slide_set_background(u, d, 0, &bad) == -2, "a background with a bad shade refused");
    CHECK(opr_slide_set_background(u, d, 1, &bg) == -1, "a slide that is not there");
    opr_undo_free(u);
    owf_deck_free(d);
}

int main(void)
{
    defaults();
    put_and_get();
    refused();
    undo_steps();
    printf("area: %d checks, %d failed\n", checks, failures);
    return failures != 0;
}
