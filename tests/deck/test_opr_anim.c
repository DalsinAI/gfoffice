/*
 * test_opr_anim: custom animation (src/deck/opr_anim.c, the show's steps in
 * src/deck/opr_show.c, the effects in ODP in libowf/src/in_odp.c and
 * out_odp.c):
 *
 *   - effects added, changed, moved and deleted, each one Undo step, and
 *     Redo; an object gets an id for its effects;
 *   - an effect follows its object through a delete and its Undo, and a
 *     duplicated slide's effects name the copy's objects;
 *   - which objects show before and after each step; steps and times of
 *     on-click, with-previous and after-previous effects, with delays;
 *   - the show plays a slide's steps before it moves on, takes them back,
 *     shows a slide reached backwards whole, and waits for them before its
 *     automatic advance;
 *   - each effect's frames: fly, wipe, fade and dissolve, in and out;
 *   - saved as ODP and read back the same; Impress's own XML read, an
 *     effect GFOpenPresentation doesn't have kept by its preset;
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

/* One slide, three shapes A, B, C after its title and body. */
static owf_deck *deck_of(opr_undo *u, int *a, int *b, int *c)
{
    owf_deck *d = owf_deck_new(OWF_DECK_16_9_WIDTH, OWF_DECK_16_9_HEIGHT);
    int sel = 0;
    if (!d)
        return NULL;
    opr_slide_insert(u, d, 0, CONTENT, &sel);
    *a = add_shape(u, d, 0, 1000, 1000);
    *b = add_shape(u, d, 0, 4000, 1000);
    *c = add_shape(u, d, 0, 7000, 1000);
    opr_undo_clear(u);
    return d;
}

static owf_anim spec(const char *word, int start, int dir, int duration, int delay)
{
    owf_anim a;
    int cls = 0, effect = 0;
    memset(&a, 0, sizeof a);
    opr_effect_at(opr_effect_word(word), &cls, &effect, NULL, NULL);
    a.cls = (unsigned char)cls;
    a.effect = (unsigned char)effect;
    a.start = (unsigned char)start;
    a.direction = (unsigned char)dir;
    a.duration = duration;
    a.delay = delay;
    return a;
}

static void names(void)
{
    int i, cls, effect;
    const char *word, *label;
    for (i = 0; i < OPR_EFFECTS; i++) {
        CHECK(!opr_effect_at(i, &cls, &effect, &word, &label) && opr_effect_word(word) == i &&
              opr_effect_index(cls, effect) == i && label[0], "effect %d's names", i);
    }
    CHECK(opr_effect_word("flyin") == 2 && opr_effect_word("FADEOUT") == 6 && opr_effect_word("FLY") < 0 &&
          opr_effect_word(NULL) < 0 && opr_effect_at(OPR_EFFECTS, NULL, NULL, NULL, NULL) < 0, "words");
    CHECK(opr_effect_index(OWF_ANIM_EMPHASIS, OWF_EFFECT_APPEAR) < 0 &&
          opr_effect_index(OWF_ANIM_ENTRANCE, OWF_EFFECT_OTHER) < 0, "no names for emphasis or other");
}

static void editing(void)
{
    opr_undo *u = opr_undo_new();
    int a, b, c, sel = 0;
    owf_deck *d = deck_of(u, &a, &b, &c);
    owf_anim fly = spec("FLYIN", OWF_START_CLICK, OWF_DIR_LEFT, 0, 0), bad;
    owf_slide *s = &d->slides[0];
    CHECK(s->objs[a].id == 0 && s->nanims == 0, "no ids or effects yet");
    CHECK(opr_anim_add(u, d, 0, a, &fly, -1) == 0 && s->nanims == 1 && s->objs[a].id > 0 &&
          s->anims[0].obj == s->objs[a].id && opr_anim_obj(d, 0, 0) == a, "A flies in: A has an id");
    fly = spec("FADEIN", OWF_START_WITH, 0, 2000, 0);
    CHECK(opr_anim_add(u, d, 0, b, &fly, -1) == 1 && s->objs[b].id > s->objs[a].id, "B fades in with it");
    fly = spec("DISAPPEAR", OWF_START_AFTER, 0, 0, 500);
    CHECK(opr_anim_add(u, d, 0, c, &fly, 0) == 0 && opr_anim_obj(d, 0, 0) == c && opr_anim_obj(d, 0, 2) == b,
          "C disappears, put first");
    CHECK(!opr_undo_undo(u, d, NULL, NULL) && s->nanims == 2 && opr_anim_obj(d, 0, 0) == a, "Undo takes C's out");
    CHECK(!opr_undo_redo(u, d, NULL, NULL) && s->nanims == 3 && opr_anim_obj(d, 0, 0) == c, "Redo");
    CHECK(opr_anim_move(u, d, 0, 0, 2) == 2 && opr_anim_obj(d, 0, 2) == c && opr_anim_obj(d, 0, 0) == a, "moved last");
    fly = spec("WIPEOUT", OWF_START_CLICK, OWF_DIR_DOWN, 700, 100);
    CHECK(opr_anim_change(u, d, 0, 2, &fly) == 2 && s->anims[2].cls == OWF_ANIM_EXIT &&
          s->anims[2].effect == OWF_EFFECT_WIPE && s->anims[2].duration == 700 && s->anims[2].delay == 100 &&
          s->anims[2].direction == OWF_DIR_DOWN && opr_anim_obj(d, 0, 2) == c, "changed to wipe out");
    CHECK(!opr_undo_undo(u, d, NULL, NULL) && s->anims[2].effect == OWF_EFFECT_APPEAR, "Undo the change");
    CHECK(!opr_undo_redo(u, d, NULL, NULL) && s->anims[2].effect == OWF_EFFECT_WIPE, "Redo it");
    CHECK(opr_anim_delete(u, d, 0, 1) == 1 && s->nanims == 2 && opr_anim_obj(d, 0, 1) == c, "B's deleted");
    CHECK(!opr_undo_undo(u, d, NULL, NULL) && s->nanims == 3 && opr_anim_obj(d, 0, 1) == b, "and back");

    /* the object goes: its effect waits for it */
    CHECK(!opr_obj_delete(u, d, 0, a) && opr_anim_obj(d, 0, 0) < 0 && opr_anim_steps(d, 0) == 1,
          "A deleted: its effect is not played");
    CHECK(!opr_undo_undo(u, d, NULL, NULL) && opr_anim_obj(d, 0, 0) == a && opr_anim_steps(d, 0) == 2,
          "A back with its effect");
    CHECK(opr_anim_delete_obj(u, d, 0, a) == 1 && s->nanims == 2, "A's effects deleted with it");
    CHECK(!opr_undo_undo(u, d, NULL, NULL) && s->nanims == 3, "and back");

    /* a copy of the slide animates its own objects */
    CHECK(!opr_slide_duplicate(u, d, 0, 1, &sel) && d->nslides == 2, "duplicated");
    s = &d->slides[0];
    {
        owf_slide *t = &d->slides[1];
        int i, ok = t->nanims == 3;
        for (i = 0; ok && i < 3; i++) {
            int k = opr_anim_obj(d, 1, i);
            ok = k == opr_anim_obj(d, 0, i) && t->objs[k].id > 0 && t->objs[k].id != s->objs[k].id;
        }
        CHECK(ok, "the copy's effects name its objects, with ids of their own");
    }

    /* refusals */
    bad = spec("APPEAR", OWF_START_CLICK, 0, 0, 0);
    bad.duration = 600001;
    CHECK(opr_anim_add(u, d, 0, a, &bad, -1) == -2, "too long");
    bad = spec("APPEAR", 7, 0, 0, 0);
    CHECK(opr_anim_add(u, d, 0, a, &bad, -1) == -2, "a start that is not one");
    bad = spec("APPEAR", OWF_START_CLICK, 0, 0, 0);
    CHECK(opr_anim_add(u, d, 0, 99, &bad, -1) == -2 && opr_anim_change(u, d, 0, 9, &bad) == -2 &&
          opr_anim_delete(u, d, 0, 9) == -2 && opr_anim_move(u, d, 0, 0, 9) == -2 &&
          opr_anim_add(u, d, 5, a, &bad, -1) == -1, "out of range");
    owf_deck_free(d);
    opr_undo_free(u);
}

/* A, B and C: A flies in on a click; B fades in with it (2 s); C appears after them, half a second on;
   then a click wipes A out. Before all that, nothing: B's first effect is an entrance, so it starts
   hidden; the title and body show throughout. */
static owf_deck *story(opr_undo *u, int *a, int *b, int *c)
{
    owf_deck *d = deck_of(u, a, b, c);
    owf_anim x;
    x = spec("FLYIN", OWF_START_CLICK, OWF_DIR_LEFT, 0, 0);       opr_anim_add(u, d, 0, *a, &x, -1);
    x = spec("FADEIN", OWF_START_WITH, 0, 2000, 0);               opr_anim_add(u, d, 0, *b, &x, -1);
    x = spec("APPEAR", OWF_START_AFTER, 0, 0, 500);               opr_anim_add(u, d, 0, *c, &x, -1);
    x = spec("WIPEOUT", OWF_START_CLICK, OWF_DIR_RIGHT, 400, 0);  opr_anim_add(u, d, 0, *a, &x, -1);
    return d;
}

static void playing(void)
{
    opr_undo *u = opr_undo_new();
    int a, b, c, n;
    unsigned char v[8];
    opr_anim_play p[8];
    long total;
    owf_deck *d = story(u, &a, &b, &c);
    CHECK(opr_anim_steps(d, 0) == 2, "two clicks");
    CHECK(!opr_anim_visible(d, 0, 0, v) && v[0] && v[1] && !v[a] && !v[b] && !v[c], "before: A, B, C hidden");
    CHECK(!opr_anim_visible(d, 0, 1, v) && v[a] && v[b] && v[c], "after the first click: all three");
    CHECK(!opr_anim_visible(d, 0, 2, v) && !v[a] && v[b] && v[c], "after the second: A gone");
    n = opr_anim_step_plan(d, 0, 1, p, 8, &total);
    CHECK(n == 3 && p[0].obj == a && p[0].begin == 0 && p[0].duration == 500 &&
          p[1].obj == b && p[1].begin == 0 && p[1].duration == 2000 &&
          p[2].obj == c && p[2].begin == 2500 && p[2].duration == 0 && total == 2500,
          "step 1's times: %d effects, %ld+%ld %ld+%ld %ld+%ld, %ld", n, p[0].begin, p[0].duration,
          p[1].begin, p[1].duration, p[2].begin, p[2].duration, total);
    n = opr_anim_step_plan(d, 0, 2, p, 8, &total);
    CHECK(n == 1 && p[0].obj == a && p[0].cls == OWF_ANIM_EXIT && total == 400, "step 2");
    CHECK(opr_anim_step_plan(d, 0, 0, p, 8, &total) == 0 && total == 0, "nothing before the first click");
    CHECK(opr_anim_step_plan(d, 0, 3, p, 8, NULL) == 0, "no step 3");
    {
        /* an effect after previous at the start plays as the slide comes in */
        owf_anim x = spec("APPEAR", OWF_START_AFTER, 0, 0, 300);
        opr_anim_add(u, d, 0, 1, &x, 0);
        n = opr_anim_step_plan(d, 0, 0, p, 8, &total);
        CHECK(n == 1 && p[0].obj == 1 && p[0].begin == 300 && opr_anim_steps(d, 0) == 2, "step 0 plays by itself");
        CHECK(!opr_anim_visible(d, 0, -1, v) && !v[1] && !opr_anim_visible(d, 0, 0, v) && v[1], "the body comes in");
        opr_undo_undo(u, d, NULL, NULL);
    }
    owf_deck_free(d);
    opr_undo_free(u);
}

static void showing(void)
{
    opr_undo *u = opr_undo_new();
    int a, b, c, sel = 0;
    owf_deck *d = story(u, &a, &b, &c);
    opr_show show;
    opr_slide_insert(u, d, 1, CONTENT, &sel);
    d->slides[0].transition.advance_after = 1000;
    CHECK(!opr_show_begin(&show, d, 0, 0) && show.slide == 0 && show.steps == 2 && show.step == 0, "begun");
    CHECK(opr_show_tick(&show, 5000) == 0 && show.slide == 0, "the timer waits for the clicks");
    CHECK(opr_show_next(&show) == 2 && show.step == 1 && show.slide == 0, "click: step 1");
    CHECK(opr_show_next(&show) == 2 && show.step == 2, "click: step 2");
    CHECK(opr_show_tick(&show, 999) == 0 && opr_show_tick(&show, 1) == 1 && show.slide == 1 && show.step == 0 &&
          show.steps == 0, "then the timer moves on");
    CHECK(opr_show_previous(&show) == 1 && show.slide == 0 && show.step == 2, "back: the slide whole");
    CHECK(opr_show_previous(&show) == 2 && show.step == 1, "back a step");
    CHECK(opr_show_goto(&show, 0) == 0 && show.step == 0, "a jump starts it again");
    owf_deck_free(d);
    opr_undo_free(u);
}

static opr_anim_play play(int cls, int effect, int dir)
{
    opr_anim_play p;
    memset(&p, 0, sizeof p);
    p.cls = cls;
    p.effect = effect;
    p.direction = dir;
    p.begin = 100;
    p.duration = 1000;
    return p;
}

static void frames(void)
{
    opr_anim_look l;
    opr_anim_play p = play(OWF_ANIM_ENTRANCE, OWF_EFFECT_FLY, OWF_DIR_LEFT);
    /* a 100 x 50 box at 300, 200 on a 640 x 360 slide at 0, 0 */
    opr_anim_look_at(&p, 0, 300, 200, 100, 50, 0, 0, 640, 360, &l);
    CHECK(!l.show, "an entrance before it starts");
    opr_anim_look_at(&p, 100, 300, 200, 100, 50, 0, 0, 640, 360, &l);
    CHECK(l.show && l.dx == -400 && l.dy == 0 && l.clip_x == -100, "fly from the left starts off the slide: %d", l.dx);
    opr_anim_look_at(&p, 600, 300, 200, 100, 50, 0, 0, 640, 360, &l);
    CHECK(l.show && l.dx == -200, "half way: %d", l.dx);
    opr_anim_look_at(&p, 1100, 300, 200, 100, 50, 0, 0, 640, 360, &l);
    CHECK(l.show && l.dx == 0 && l.dy == 0 && l.amount == 1000, "there");
    p = play(OWF_ANIM_EXIT, OWF_EFFECT_FLY, OWF_DIR_DOWN);
    opr_anim_look_at(&p, 600, 300, 200, 100, 50, 0, 0, 640, 360, &l);
    CHECK(l.show && l.dx == 0 && l.dy == 80, "fly out down, half way: %d", l.dy);
    opr_anim_look_at(&p, 1100, 300, 200, 100, 50, 0, 0, 640, 360, &l);
    CHECK(!l.show, "gone after it");
    p = play(OWF_ANIM_ENTRANCE, OWF_EFFECT_WIPE, OWF_DIR_RIGHT);
    opr_anim_look_at(&p, 350, 300, 200, 100, 50, 0, 0, 640, 360, &l);
    CHECK(l.show && l.clip_w == 25 && l.clip_x == 375 && l.clip_h == 50, "wipe from the right: a quarter");
    p = play(OWF_ANIM_EXIT, OWF_EFFECT_WIPE, OWF_DIR_UP);
    opr_anim_look_at(&p, 350, 300, 200, 100, 50, 0, 0, 640, 360, &l);
    CHECK(l.show && l.clip_h == 37 && l.clip_y == 213, "wipe out from the top: %d at %d", l.clip_h, l.clip_y);
    p = play(OWF_ANIM_ENTRANCE, OWF_EFFECT_FADE, 0);
    opr_anim_look_at(&p, 600, 300, 200, 100, 50, 0, 0, 640, 360, &l);
    CHECK(l.show && l.amount == 500, "fade in half");
    p = play(OWF_ANIM_EXIT, OWF_EFFECT_DISSOLVE, 0);
    opr_anim_look_at(&p, 850, 300, 200, 100, 50, 0, 0, 640, 360, &l);
    CHECK(l.show && l.amount == 250, "dissolve out three quarters");
    opr_anim_look_at(&p, 0, 300, 200, 100, 50, 0, 0, 640, 360, &l);
    CHECK(l.show && l.amount == 1000, "an exit shows before it starts");
    p = play(OWF_ANIM_ENTRANCE, OWF_EFFECT_APPEAR, 0);
    p.duration = 0;
    opr_anim_look_at(&p, 100, 300, 200, 100, 50, 0, 0, 640, 360, &l);
    CHECK(l.show, "appear at its start");
    CHECK(opr_anim_look_at(NULL, 0, 0, 0, 1, 1, 0, 0, 1, 1, &l) < 0, "refused");
}

static owf_deck *round_trip(const owf_deck *d, const char *format)
{
    const owf_deck_format *f = owf_deck_format_named(format);
    unsigned char *data = NULL;
    size_t n = 0;
    owf_deck *back = NULL;
    if (!f || !f->export || f->export(d, &data, &n, NULL) != OWF_OK)
        return NULL;
    if (owf_import_deck_memory(data, n, format, &back, NULL, NULL) != OWF_OK)
        back = NULL;
    free(data);
    return back;
}

static int same_effects(const owf_deck *x, const owf_deck *y, int slide)
{
    const owf_slide *s = &x->slides[slide], *t = &y->slides[slide];
    int i;
    if (s->nanims != t->nanims)
        return 0;
    for (i = 0; i < s->nanims; i++) {
        const owf_anim *a = &s->anims[i], *b = &t->anims[i];
        if (opr_anim_obj(x, slide, i) != opr_anim_obj(y, slide, i) || a->cls != b->cls || a->effect != b->effect ||
            a->start != b->start || a->delay != b->delay || opr_effect_duration(a) != opr_effect_duration(b) ||
            ((a->effect == OWF_EFFECT_FLY || a->effect == OWF_EFFECT_WIPE) && a->direction != b->direction)) {
            printf("  effect %d: obj %d/%d cls %d/%d effect %d/%d start %d/%d delay %d/%d dur %d/%d dir %d/%d\n", i,
                   opr_anim_obj(x, slide, i), opr_anim_obj(y, slide, i), a->cls, b->cls, a->effect, b->effect,
                   a->start, b->start, a->delay, b->delay, opr_effect_duration(a), opr_effect_duration(b),
                   a->direction, b->direction);
            return 0;
        }
    }
    return 1;
}

static void saving(void)
{
    opr_undo *u = opr_undo_new();
    int a, b, c, i;
    owf_deck *d = story(u, &a, &b, &c), *back;
    static const char *words[] = { "DISSOLVEIN", "FADEOUT", "FLYOUT", "DISSOLVEOUT", "WIPEIN" };
    for (i = 0; i < 5; i++) {
        owf_anim x = spec(words[i], i & 1 ? OWF_START_CLICK : OWF_START_WITH, i % 4, 300 * (i + 1), 100 * i);
        opr_anim_add(u, d, 0, i & 1 ? b : c, &x, -1);
    }
    back = round_trip(d, "odp");
    CHECK(back && back->nslides == 1 && same_effects(d, back, 0), "ODP: the effects as they were");
    CHECK(back && back->slides[0].keep_timing >= 0, "read back, the file's XML is kept");
    if (back) {
        owf_deck *again = round_trip(back, "odp");
        CHECK(again && same_effects(d, again, 0), "saved again from the kept XML");
        owf_deck_free(again);
        owf_deck_free(back);
    }
    back = round_trip(d, "fodp");
    CHECK(back && same_effects(d, back, 0), "flat ODP too");
    owf_deck_free(back);
    /* an effect whose object went is not saved */
    opr_obj_delete(u, d, 0, c);
    back = round_trip(d, "odp");
    CHECK(back && back->slides[0].nanims == d->slides[0].nanims - 4, "C's four effects left out: %d",
          back ? back->slides[0].nanims : -1);
    owf_deck_free(back);
    owf_deck_free(d);
    opr_undo_free(u);
}

/* Impress's own content.xml for a slide: a zoom (which GFOpenPresentation doesn't have), a fly in from the
   top after it, an interactive sequence, an object with an xml:id of its own. */
static const char impress[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
    "<office:document xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" "
    "xmlns:draw=\"urn:oasis:names:tc:opendocument:xmlns:drawing:1.0\" "
    "xmlns:svg=\"urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0\" "
    "xmlns:presentation=\"urn:oasis:names:tc:opendocument:xmlns:presentation:1.0\" "
    "xmlns:smil=\"urn:oasis:names:tc:opendocument:xmlns:smil-compatible:1.0\" "
    "xmlns:anim=\"urn:oasis:names:tc:opendocument:xmlns:animation:1.0\" "
    "xmlns:xml=\"http://www.w3.org/XML/1998/namespace\" "
    "xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\" "
    "office:version=\"1.2\" office:mimetype=\"application/vnd.oasis.opendocument.presentation\">"
    "<office:body><office:presentation><draw:page draw:name=\"one\">"
    "<draw:rect draw:id=\"id7\" xml:id=\"id7\" svg:x=\"1cm\" svg:y=\"1cm\" svg:width=\"2cm\" svg:height=\"2cm\"/>"
    "<draw:rect xml:id=\"logo\" svg:x=\"5cm\" svg:y=\"1cm\" svg:width=\"2cm\" svg:height=\"2cm\"/>"
    "<anim:par presentation:node-type=\"timing-root\"><anim:seq presentation:node-type=\"main-sequence\">"
    "<anim:par smil:begin=\"indefinite\"><anim:par smil:begin=\"0s\">"
    "<anim:par smil:begin=\"0s\" smil:fill=\"hold\" presentation:node-type=\"on-click\" "
    "presentation:preset-class=\"entrance\" presentation:preset-id=\"ooo-entrance-zoom\">"
    "<anim:set smil:begin=\"0s\" smil:dur=\"0.001s\" smil:fill=\"hold\" smil:targetElement=\"id7\" "
    "smil:attributeName=\"visibility\" smil:to=\"visible\"/>"
    "<anim:animate smil:dur=\"0.5s\" smil:fill=\"hold\" smil:targetElement=\"id7\" smil:attributeName=\"width\" "
    "smil:values=\"0;width\" smil:keyTimes=\"0;1\"/></anim:par></anim:par>"
    "<anim:par smil:begin=\"0.5s\">"
    "<anim:par smil:begin=\"0.25s\" smil:fill=\"hold\" presentation:node-type=\"after-previous\" "
    "presentation:preset-class=\"entrance\" presentation:preset-id=\"ooo-entrance-fly-in\" "
    "presentation:preset-sub-type=\"from-top\">"
    "<anim:set smil:begin=\"0s\" smil:dur=\"0.001s\" smil:fill=\"hold\" smil:targetElement=\"logo\" "
    "smil:attributeName=\"visibility\" smil:to=\"visible\"/>"
    "<anim:animate smil:dur=\"1.5s\" smil:fill=\"hold\" smil:targetElement=\"logo\" smil:attributeName=\"y\" "
    "smil:values=\"0-height/2;y\" smil:keyTimes=\"0;1\"/></anim:par></anim:par></anim:par>"
    "</anim:seq><anim:seq presentation:node-type=\"interactive-sequence\"/></anim:par>"
    "</draw:page></office:presentation></office:body></office:document>";

static void impress_xml(void)
{
    owf_deck *d = NULL;
    owf_report *report = owf_report_new();
    const owf_anim *a;
    CHECK(owf_import_deck_memory((const unsigned char *)impress, sizeof impress - 1, "fodp", &d, report, NULL) == OWF_OK &&
          d && d->nslides == 1, "Impress's XML read");
    if (!d) { owf_report_free(report); return; }
    CHECK(d->slides[0].nanims == 2 && d->slides[0].keep_timing >= 0, "two effects, the XML kept: %d",
          d->slides[0].nanims);
    if (d->slides[0].nanims == 2) {
        a = &d->slides[0].anims[0];
        CHECK(a->cls == OWF_ANIM_ENTRANCE && a->effect == OWF_EFFECT_OTHER && a->preset &&
              !strcmp(a->preset, "ooo-entrance-zoom") && a->start == OWF_START_CLICK && a->duration == 500 &&
              opr_anim_obj(d, 0, 0) == 0, "the zoom, kept by its preset");
        a = &d->slides[0].anims[1];
        CHECK(a->effect == OWF_EFFECT_FLY && a->direction == OWF_DIR_UP && a->start == OWF_START_AFTER &&
              a->delay == 250 && a->duration == 1500 && opr_anim_obj(d, 0, 1) == 1 && d->slides[0].objs[1].id > 0 &&
              d->slides[0].objs[1].xml_id && !strcmp(d->slides[0].objs[1].xml_id, "logo"),
              "the fly in from the top, its object given an id");
        CHECK(opr_anim_steps(d, 0) == 1, "one click");
    }
    CHECK(owf_report_count(report) > 0, "the zoom and the interactive sequence are reported");
    {
        /* changed in GFOpenPresentation: written from the model, the zoom by its preset */
        opr_undo *u = opr_undo_new();
        owf_anim x = spec("APPEAR", OWF_START_CLICK, 0, 0, 0);
        owf_deck *back;
        CHECK(opr_anim_add(u, d, 0, 0, &x, -1) == 2 && d->slides[0].keep_timing < 0, "an effect added: XML dropped");
        back = round_trip(d, "odp");
        CHECK(back && same_effects(d, back, 0) && back->slides[0].anims[0].preset &&
              !strcmp(back->slides[0].anims[0].preset, "ooo-entrance-zoom"), "saved from the model");
        owf_deck_free(back);
        CHECK(!opr_undo_undo(u, d, NULL, NULL) && d->slides[0].keep_timing >= 0, "Undo brings the XML back");
        opr_undo_free(u);
    }
    owf_report_free(report);
    owf_deck_free(d);
}

int main(void)
{
    names();
    editing();
    playing();
    showing();
    frames();
    saving();
    impress_xml();
    printf("test_opr_anim: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
