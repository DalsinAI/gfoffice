/*
 * GFOpenPresentation's custom animation: a slide's effects as the editor
 * changes them (each change one Undo step through opr_slide_set_anims) and
 * as the show plays them, step by step, with the geometry of each frame.
 * The show's drawing stays the app's; this says what to draw where.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "openpresentation_core.h"

#include <ctype.h>
#include <string.h>

static const struct {
    unsigned char cls, effect;
    const char *word, *label;
} effects[OPR_EFFECTS] = {
    { OWF_ANIM_ENTRANCE, OWF_EFFECT_APPEAR, "APPEAR", "Appear" },
    { OWF_ANIM_ENTRANCE, OWF_EFFECT_FADE, "FADEIN", "Fade In" },
    { OWF_ANIM_ENTRANCE, OWF_EFFECT_FLY, "FLYIN", "Fly In" },
    { OWF_ANIM_ENTRANCE, OWF_EFFECT_WIPE, "WIPEIN", "Wipe In" },
    { OWF_ANIM_ENTRANCE, OWF_EFFECT_DISSOLVE, "DISSOLVEIN", "Dissolve In" },
    { OWF_ANIM_EXIT, OWF_EFFECT_APPEAR, "DISAPPEAR", "Disappear" },
    { OWF_ANIM_EXIT, OWF_EFFECT_FADE, "FADEOUT", "Fade Out" },
    { OWF_ANIM_EXIT, OWF_EFFECT_FLY, "FLYOUT", "Fly Out" },
    { OWF_ANIM_EXIT, OWF_EFFECT_WIPE, "WIPEOUT", "Wipe Out" },
    { OWF_ANIM_EXIT, OWF_EFFECT_DISSOLVE, "DISSOLVEOUT", "Dissolve Out" }
};

int opr_effect_at(int n, int *cls, int *effect, const char **word, const char **label)
{
    if (n < 0 || n >= OPR_EFFECTS)
        return -1;
    if (cls) *cls = effects[n].cls;
    if (effect) *effect = effects[n].effect;
    if (word) *word = effects[n].word;
    if (label) *label = effects[n].label;
    return 0;
}

int opr_effect_index(int cls, int effect)
{
    int i;
    for (i = 0; i < OPR_EFFECTS; i++)
        if (effects[i].cls == cls && effects[i].effect == effect)
            return i;
    return -1;
}

int opr_effect_word(const char *word)
{
    int i, j;
    if (!word)
        return -1;
    for (i = 0; i < OPR_EFFECTS; i++) {
        const char *w = effects[i].word;
        for (j = 0; w[j] && word[j] && toupper((unsigned char)word[j]) == w[j]; j++)
            ;
        if (!w[j] && !word[j])
            return i;
    }
    return -1;
}

int opr_effect_duration(const owf_anim *a)
{
    if (!a)
        return 0;
    if (a->duration > 0)
        return a->duration;
    switch (a->effect) {
    case OWF_EFFECT_FLY: case OWF_EFFECT_WIPE: return 500;
    case OWF_EFFECT_FADE: case OWF_EFFECT_DISSOLVE: return 1000;
    default: return 0;
    }
}

static const owf_slide *slide_at(const owf_deck *deck, int slide)
{
    return deck && slide >= 0 && slide < deck->nslides ? &deck->slides[slide] : NULL;
}

int opr_anim_obj(const owf_deck *deck, int slide, int index)
{
    const owf_slide *s = slide_at(deck, slide);
    if (!s || index < 0 || index >= s->nanims)
        return -1;
    return owf_slide_obj_by_id(s, s->anims[index].obj);
}

/* ---- Changes ---- */

static char *text_copy(const char *t)
{
    size_t n;
    char *c;
    if (!t)
        return NULL;
    n = strlen(t);
    c = owf_malloc(n + 1);
    if (c)
        memcpy(c, t, n + 1);
    return c;
}

static void anims_free(owf_anim *a, int n)
{
    int i;
    for (i = 0; i < n; i++)
        owf_free(a[i].preset);
    owf_free(a);
}

/* A copy of the slide's effects with room for one more; *cap gets its size. */
static owf_anim *anims_copy(const owf_slide *s, int *cap)
{
    owf_anim *a;
    int i;
    *cap = s->nanims + 1;
    a = owf_malloc((size_t)*cap * sizeof *a);
    if (!a)
        return NULL;
    memset(a, 0, (size_t)*cap * sizeof *a);
    for (i = 0; i < s->nanims; i++) {
        a[i] = s->anims[i];
        a[i].preset = text_copy(s->anims[i].preset);
        if (s->anims[i].preset && !a[i].preset) {
            anims_free(a, i);
            return NULL;
        }
    }
    return a;
}

static int spec_ok(const owf_anim *spec)
{
    return spec && spec->cls <= OWF_ANIM_EMPHASIS && spec->effect <= OWF_EFFECT_OTHER &&
           spec->start <= OWF_START_AFTER && spec->direction <= OWF_DIR_DOWN &&
           spec->duration >= 0 && spec->duration <= 600000 && spec->delay >= 0 && spec->delay <= 600000;
}

int opr_anim_add(opr_undo *u, owf_deck *deck, int slide, int obj, const owf_anim *spec, int at)
{
    owf_slide *s;
    owf_obj *o;
    owf_anim *a;
    long id;
    int cap;
    if (!u || !deck || slide < 0 || slide >= deck->nslides)
        return -1;
    s = &deck->slides[slide];
    if (!spec_ok(spec) || obj < 0 || obj >= s->nobjs)
        return -2;
    o = &s->objs[obj];
    id = o->id > 0 ? o->id : owf_deck_new_obj_id(deck);
    a = anims_copy(s, &cap);
    if (!a)
        return -1;
    if (at < 0 || at > s->nanims)
        at = s->nanims;
    memmove(&a[at + 1], &a[at], (size_t)(s->nanims - at) * sizeof *a);
    a[at] = *spec;
    a[at].obj = id;
    a[at].keep = -1;
    a[at].preset = text_copy(spec->preset);
    if (spec->preset && !a[at].preset) {
        anims_free(a, s->nanims + 1);
        return -1;
    }
    if (opr_slide_set_anims(u, deck, slide, a, s->nanims + 1, cap) < 0) {
        anims_free(a, s->nanims + 1);
        return -1;
    }
    o->id = id;                         /* an id is not undone: it changes nothing else */
    return at;
}

int opr_anim_change(opr_undo *u, owf_deck *deck, int slide, int index, const owf_anim *spec)
{
    owf_slide *s;
    owf_anim *a;
    int cap, n;
    if (!u || !deck || slide < 0 || slide >= deck->nslides)
        return -1;
    s = &deck->slides[slide];
    if (!spec_ok(spec) || index < 0 || index >= s->nanims)
        return -2;
    n = s->nanims;
    a = anims_copy(s, &cap);
    if (!a)
        return -1;
    if (a[index].cls != spec->cls || a[index].effect != spec->effect) {
        owf_free(a[index].preset);
        a[index].preset = NULL;
    }
    /* the file's own XML for an effect says how it starts and how long it takes */
    if (a[index].cls != spec->cls || a[index].effect != spec->effect || a[index].start != spec->start ||
        a[index].duration != spec->duration || a[index].delay != spec->delay)
        a[index].keep = -1;
    a[index].cls = spec->cls;
    a[index].effect = spec->effect;
    a[index].start = spec->start;
    a[index].direction = spec->direction;
    a[index].duration = spec->duration;
    a[index].delay = spec->delay;
    if (opr_slide_set_anims(u, deck, slide, a, n, cap) < 0) {
        anims_free(a, n);
        return -1;
    }
    return index;
}

int opr_anim_delete(opr_undo *u, owf_deck *deck, int slide, int index)
{
    owf_slide *s;
    owf_anim *a;
    int cap, n;
    if (!u || !deck || slide < 0 || slide >= deck->nslides)
        return -1;
    s = &deck->slides[slide];
    if (index < 0 || index >= s->nanims)
        return -2;
    n = s->nanims;
    a = anims_copy(s, &cap);
    if (!a)
        return -1;
    owf_free(a[index].preset);
    memmove(&a[index], &a[index + 1], (size_t)(n - index - 1) * sizeof *a);
    if (opr_slide_set_anims(u, deck, slide, a, n - 1, cap) < 0) {
        anims_free(a, n - 1);
        return -1;
    }
    return index;
}

int opr_anim_move(opr_undo *u, owf_deck *deck, int slide, int from, int to)
{
    owf_slide *s;
    owf_anim *a, moved;
    int cap, n;
    if (!u || !deck || slide < 0 || slide >= deck->nslides)
        return -1;
    s = &deck->slides[slide];
    n = s->nanims;
    if (from < 0 || from >= n || to < 0 || to >= n)
        return -2;
    if (from == to)
        return to;
    a = anims_copy(s, &cap);
    if (!a)
        return -1;
    moved = a[from];
    if (from < to)
        memmove(&a[from], &a[from + 1], (size_t)(to - from) * sizeof *a);
    else
        memmove(&a[to + 1], &a[to], (size_t)(from - to) * sizeof *a);
    a[to] = moved;
    if (opr_slide_set_anims(u, deck, slide, a, n, cap) < 0) {
        anims_free(a, n);
        return -1;
    }
    return to;
}

int opr_anim_delete_obj(opr_undo *u, owf_deck *deck, int slide, int obj)
{
    owf_slide *s;
    owf_anim *a;
    long id;
    int cap, n, i, kept = 0;
    if (!u || !deck || slide < 0 || slide >= deck->nslides)
        return -1;
    s = &deck->slides[slide];
    if (obj < 0 || obj >= s->nobjs)
        return -2;
    id = s->objs[obj].id;
    if (id <= 0)
        return 0;
    for (i = 0; i < s->nanims; i++)
        if (s->anims[i].obj == id)
            break;
    if (i == s->nanims)
        return 0;
    n = s->nanims;
    a = anims_copy(s, &cap);
    if (!a)
        return -1;
    for (i = 0; i < n; i++) {
        if (a[i].obj == id)
            owf_free(a[i].preset);
        else
            a[kept++] = a[i];
    }
    if (opr_slide_set_anims(u, deck, slide, a, kept, cap) < 0) {
        anims_free(a, kept);
        return -1;
    }
    return n - kept;
}

/* ---- Playing ---- */

/* An effect the show plays: its object is on the slide. */
static int live(const owf_slide *s, int i)
{
    return owf_slide_obj_by_id(s, s->anims[i].obj) >= 0;
}

int opr_anim_steps(const owf_deck *deck, int slide)
{
    const owf_slide *s = slide_at(deck, slide);
    int i, n = 0;
    if (!s)
        return 0;
    for (i = 0; i < s->nanims; i++)
        if (live(s, i) && s->anims[i].start == OWF_START_CLICK)
            n++;
    return n;
}

int opr_anim_visible(const owf_deck *deck, int slide, int step, unsigned char *visible)
{
    const owf_slide *s = slide_at(deck, slide);
    int i, cur = 0, k;
    if (!s || !visible || step < -1)
        return -1;
    for (i = 0; i < s->nobjs; i++)
        visible[i] = 1;
    /* hidden until its entrance: an object whose first effect brings it in */
    for (i = s->nanims - 1; i >= 0; i--) {
        k = owf_slide_obj_by_id(s, s->anims[i].obj);
        if (k >= 0)
            visible[k] = s->anims[i].cls == OWF_ANIM_ENTRANCE ? 0 : 1;
    }
    for (i = 0; i < s->nanims; i++) {
        k = owf_slide_obj_by_id(s, s->anims[i].obj);
        if (k < 0)
            continue;
        if (s->anims[i].start == OWF_START_CLICK)
            cur++;
        if (cur > step)
            break;
        if (s->anims[i].cls == OWF_ANIM_ENTRANCE)
            visible[k] = 1;
        else if (s->anims[i].cls == OWF_ANIM_EXIT)
            visible[k] = 0;
    }
    /* a group's objects follow it (a group comes before its objects) */
    for (i = 0; i < s->nobjs; i++) {
        int p = s->objs[i].parent;
        if (p >= 0 && p < i && !visible[p])
            visible[i] = 0;
    }
    return 0;
}

int opr_anim_step_plan(const owf_deck *deck, int slide, int step, opr_anim_play *out, int max, long *total)
{
    const owf_slide *s = slide_at(deck, slide);
    int i, cur = 0, n = 0, first = 1;
    long base = 0, prev_base = 0, end = 0;
    if (total) *total = 0;
    if (!s || step < 0)
        return 0;
    for (i = 0; i < s->nanims; i++) {
        const owf_anim *a = &s->anims[i];
        int k = owf_slide_obj_by_id(s, a->obj);
        long begin, dur;
        if (k < 0)
            continue;
        if (a->start == OWF_START_CLICK)
            cur++;
        if (cur < step)
            continue;
        if (cur > step)
            break;
        if (first || a->start == OWF_START_CLICK)
            base = 0;
        else if (a->start == OWF_START_WITH)
            base = prev_base;
        else
            base = end;
        first = 0;
        begin = base + a->delay;
        dur = opr_effect_duration(a);
        prev_base = base;
        if (begin + dur > end)
            end = begin + dur;
        if (out && n < max) {
            out[n].anim = i;
            out[n].obj = k;
            out[n].cls = a->cls;
            out[n].effect = a->effect;
            out[n].direction = a->direction;
            out[n].begin = begin;
            out[n].duration = dur;
        }
        n++;
    }
    if (total) *total = end;
    return n;
}

int opr_anim_look_at(const opr_anim_play *p, long t, int x, int y, int w, int h,
                     int sx, int sy, int sw, int sh, opr_anim_look *look)
{
    long f;
    int amount, exit;
    if (!p || !look || w < 0 || h < 0)
        return -1;
    memset(look, 0, sizeof *look);
    look->clip_x = x;
    look->clip_y = y;
    look->clip_w = w;
    look->clip_h = h;
    look->amount = 1000;
    exit = p->cls == OWF_ANIM_EXIT;
    if (p->cls == OWF_ANIM_EMPHASIS) {
        look->show = 1;
        return 0;
    }
    if (t < p->begin) {
        look->show = exit;
        return 0;
    }
    if (p->duration <= 0 || t >= p->begin + p->duration || p->effect == OWF_EFFECT_APPEAR ||
        p->effect == OWF_EFFECT_OTHER) {
        look->show = !exit;
        return 0;
    }
    f = (t - p->begin) * 1000L / p->duration;
    amount = (int)(exit ? 1000 - f : f);    /* how much of the object is in */
    look->show = amount > 0;
    switch (p->effect) {
    case OWF_EFFECT_FADE:
    case OWF_EFFECT_DISSOLVE:
        look->amount = amount;
        break;
    case OWF_EFFECT_FLY: {
        long dx = 0, dy = 0;
        if (p->direction == OWF_DIR_LEFT) dx = sx - (x + w);
        else if (p->direction == OWF_DIR_RIGHT) dx = sx + sw - x;
        else if (p->direction == OWF_DIR_UP) dy = sy - (y + h);
        else dy = sy + sh - y;
        look->dx = (int)(dx * (1000 - amount) / 1000);
        look->dy = (int)(dy * (1000 - amount) / 1000);
        look->clip_x = x + look->dx;
        look->clip_y = y + look->dy;
        look->show = 1;
        break;
    }
    case OWF_EFFECT_WIPE: {
        int cw = (int)((long)w * amount / 1000), ch = (int)((long)h * amount / 1000);
        /* in: uncovered from the edge it comes from; out: covered from it */
        int from_end = (p->direction == OWF_DIR_RIGHT || p->direction == OWF_DIR_DOWN) != exit;
        if (p->direction == OWF_DIR_LEFT || p->direction == OWF_DIR_RIGHT) {
            look->clip_w = cw;
            look->clip_x = from_end ? x + w - cw : x;
        } else {
            look->clip_h = ch;
            look->clip_y = from_end ? y + h - ch : y;
        }
        look->show = cw > 0 && ch > 0;
        break;
    }
    default:
        break;
    }
    return 0;
}
