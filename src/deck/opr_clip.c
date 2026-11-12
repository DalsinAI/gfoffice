/* GFOpenPresentation core: slides from another deck (Impress parity P8):
 * Insert Slides from File, and the sorter's Cut, Copy and Paste of whole
 * slides, which go through a deck of their own (opr_clip_*).
 *
 * The slides are copied with what they use: each picture is added to the
 * deck's pictures, each font found or added by name, each master found by
 * name or brought along, each layout found by name. Objects the file kept as
 * XML the model cannot draw (charts, embedded objects) are left out and
 * counted; effects and transitions the model has no player for become plain
 * ones; names that are taken get " (2)", " (3)"... All of it is one Undo
 * step. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "openpresentation_core.h"

typedef struct {
    owf_deck *dst;
    const owf_deck *src;
    int *images;                 /* src picture -> dst picture; -2 not yet */
    long next_id;
} importer;

static char *dup_string(const char *s)
{
    char *c;
    size_t n;
    if (!s)
        return NULL;
    n = strlen(s) + 1;
    c = malloc(n);
    if (c)
        memcpy(c, s, n);
    return c;
}

static int same_name(const char *a, const char *b)
{
    return (!a || !*a) ? (!b || !*b) : (b && !strcmp(a, b));
}

static int font_of(importer *im, int font)
{
    const owf_doc *r = im->src->res;
    int f;
    if (font < 0 || !r || font >= r->nfonts || !im->dst->res)
        return -1;
    f = owf_doc_font(im->dst->res, r->fonts[font].name ? r->fonts[font].name : "", r->fonts[font].kind);
    return f < 0 ? -1 : f;
}

static int image_of(importer *im, int image)
{
    const owf_doc *r = im->src->res;
    if (image < 0 || !r || image >= r->nimages || !im->dst->res || !im->images)
        return -1;
    if (im->images[image] == -2) {
        const owf_image *g = &r->images[image];
        im->images[image] = owf_doc_add_image(im->dst->res, g->name, g->mime, g->data, g->length,
                                              g->width, g->height, g->alt);
    }
    return im->images[image];
}

static void remap_level(importer *im, owf_textlevel *l)
{
    l->chars.font = font_of(im, l->chars.font);
    if (l->bullet_font >= 0)
        l->bullet_font = font_of(im, l->bullet_font);
}

static int remap_obj(importer *im, owf_obj *o)
{
    int i, j;
    for (i = 0; i < o->text.nparas; i++) {
        owf_para *p = &o->text.paras[i];
        for (j = 0; j < p->nruns; j++) {
            owf_run *r = &p->runs[j];
            r->fmt.font = font_of(im, r->fmt.font);
            if (r->kind == OWF_RUN_IMAGE) {
                r->image = image_of(im, r->image);
                if (r->image < 0)
                    return -1;
            }
            r->revision = r->comment = r->note = 0;
        }
    }
    if (o->list)
        for (i = 0; i < OWF_TEXT_LEVELS; i++)
            remap_level(im, &o->list[i]);
    if (o->kind == OWF_OBJ_PICTURE && o->image >= 0) {
        o->image = image_of(im, o->image);
        if (o->image < 0)
            return -1;
    }
    o->flow_story = -1;
    o->keep = -1;
    return 0;
}

/* The src objects that come (not kept XML) into to, their groups following; ids[i] gets object i's new id
 * when ids is given and it had one. 0, or -1 when memory ran out. */
static int copy_objects(importer *im, owf_obj **to, int *nto, int *capto, const owf_obj *from, int count,
                        long *ids, int *left_out)
{
    int *where, i, n = 0;
    *to = NULL;
    *nto = *capto = 0;
    if (count <= 0)
        return 0;
    where = malloc((size_t)count * sizeof *where);
    *to = calloc((size_t)count, sizeof **to);
    if (!where || !*to) {
        free(where);
        free(*to);
        *to = NULL;
        return -1;
    }
    *capto = count;
    for (i = 0; i < count; i++) {
        where[i] = -1;
        if (from[i].keep >= 0) {
            if (left_out) (*left_out)++;
            continue;
        }
        if (owf_obj_copy(&(*to)[n], &from[i]) != 0)
            goto fail;
        *nto = n + 1;
        if (remap_obj(im, &(*to)[n]) < 0)
            goto fail;
        if (ids && from[i].id)
            ids[i] = (*to)[n].id = im->next_id++;
        where[i] = n++;
    }
    for (i = 0; i < count; i++)
        if (where[i] >= 0) {
            int p = from[i].parent;
            (*to)[where[i]].parent = p >= 0 && p < count ? where[p] : -1;
        }
    free(where);
    return 0;
fail:
    for (i = 0; i < *nto; i++)
        owf_obj_clear(&(*to)[i]);
    free(*to);
    free(where);
    *to = NULL;
    *nto = *capto = 0;
    return -1;
}

static void slide_free(owf_slide *s)
{
    int i;
    for (i = 0; i < s->nobjs; i++)
        owf_obj_clear(&s->objs[i]);
    free(s->objs);
    owf_obj_clear(&s->notes);
    free(s->name);
    free(s->footer);
    free(s->date_text);
    for (i = 0; i < s->nanims; i++)
        free(s->anims[i].preset);
    free(s->anims);
    memset(s, 0, sizeof *s);
}

static int name_taken(const owf_deck *d, const char *name)
{
    int i;
    for (i = 0; i < d->nslides; i++)
        if (d->slides[i].name && !strcmp(d->slides[i].name, name))
            return 1;
    return 0;
}

static int copy_slide(importer *im, owf_slide *to, const owf_slide *from, int master, int *left_out)
{
    long *ids = NULL;
    int i, k;
    memset(to, 0, sizeof *to);
    owf_obj_init(&to->notes, OWF_OBJ_TEXT);
    to->notes.placeholder = OWF_PH_NOTES;
    to->master = master;
    to->layout = -1;
    if (from->layout >= 0 && from->layout < im->src->nlayouts)
        for (k = 0; k < im->dst->nlayouts; k++)
            if (same_name(im->dst->layouts[k].name, im->src->layouts[from->layout].name) &&
                (im->dst->layouts[k].master < 0 || im->dst->layouts[k].master == master)) {
                to->layout = k;
                break;
            }
    to->transition = from->transition;
    if (to->transition.kind == OWF_TRANS_OTHER)
        to->transition.kind = OWF_TRANS_FADE;
    to->transition.keep = -1;
    to->background = from->background;
    to->background_image = from->background_image >= 0 ? image_of(im, from->background_image) : -1;
    to->hidden = from->hidden;
    to->keep_timing = -1;
    to->show = from->show;
    if (from->nobjs) {
        ids = calloc((size_t)from->nobjs, sizeof *ids);
        if (!ids)
            return -1;
    }
    if (copy_objects(im, &to->objs, &to->nobjs, &to->capobjs, from->objs, from->nobjs, ids, left_out) < 0)
        goto fail;
    owf_obj_clear(&to->notes);
    if (owf_obj_copy(&to->notes, &from->notes) != 0) {
        owf_obj_init(&to->notes, OWF_OBJ_TEXT);
        goto fail;
    }
    remap_obj(im, &to->notes);
    to->footer = dup_string(from->footer);
    to->date_text = dup_string(from->date_text);
    if ((from->footer && !to->footer) || (from->date_text && !to->date_text))
        goto fail;
    /* the name, made free in this deck */
    if (from->name && *from->name) {
        char name[256];
        snprintf(name, sizeof name, "%.230s", from->name);
        for (k = 2; name_taken(im->dst, name) && k < 10000; k++)
            snprintf(name, sizeof name, "%.230s (%d)", from->name, k);
        to->name = dup_string(name);
        if (!to->name)
            goto fail;
    }
    /* the effects whose objects came, naming the copies by their new ids */
    if (from->nanims) {
        to->anims = calloc((size_t)from->nanims, sizeof *to->anims);
        if (!to->anims)
            goto fail;
        to->capanims = from->nanims;
        for (i = 0; i < from->nanims; i++) {
            const owf_anim *a = &from->anims[i];
            int o = owf_slide_obj_by_id(from, a->obj);
            owf_anim *b;
            if (o < 0 || !ids || !ids[o])
                continue;
            b = &to->anims[to->nanims++];
            *b = *a;
            b->obj = ids[o];
            b->preset = dup_string(a->preset);
            if (a->preset && !b->preset)
                goto fail;
            if (b->effect == OWF_EFFECT_OTHER)
                b->effect = OWF_EFFECT_APPEAR;
            b->keep = -1;
        }
    }
    free(ids);
    return 0;
fail:
    free(ids);
    slide_free(to);
    return -1;
}

/* A copy of src's master, its pictures and fonts made the deck's. */
static int copy_master(importer *im, owf_master *to, const owf_master *from)
{
    int k;
    *to = *from;
    to->objs = NULL;
    to->nobjs = to->capobjs = 0;
    to->name = dup_string(from->name);
    if (from->name && !to->name)
        return -1;
    remap_level(im, &to->title);
    remap_level(im, &to->subtitle);
    for (k = 0; k < OWF_TEXT_LEVELS; k++)
        remap_level(im, &to->body[k]);
    remap_level(im, &to->notes);
    remap_level(im, &to->other);
    to->background_image = from->background_image >= 0 ? image_of(im, from->background_image) : -1;
    return copy_objects(im, &to->objs, &to->nobjs, &to->capobjs, from->objs, from->nobjs, NULL, NULL);
}

/* A copy of one of the deck's own masters, as it is. */
static int copy_own_master(owf_master *to, const owf_master *from)
{
    int i;
    *to = *from;
    to->objs = NULL;
    to->nobjs = to->capobjs = 0;
    to->name = dup_string(from->name);
    if (from->name && !to->name)
        return -1;
    if (from->nobjs) {
        to->objs = calloc((size_t)from->nobjs, sizeof *to->objs);
        if (!to->objs)
            return -1;
        to->capobjs = from->nobjs;
        for (i = 0; i < from->nobjs; i++) {
            if (owf_obj_copy(&to->objs[i], &from->objs[i]) != 0)
                return -1;
            to->objs[i].id = from->objs[i].id;
            to->nobjs = i + 1;
        }
    }
    return 0;
}

static void free_masters(owf_master *m, int n)
{
    int i, k;
    if (!m)
        return;
    for (i = 0; i < n; i++) {
        for (k = 0; k < m[i].nobjs; k++)
            owf_obj_clear(&m[i].objs[k]);
        free(m[i].objs);
        free(m[i].name);
    }
    free(m);
}

/* The deck's masters and the new ones (copies of src's new_from) as one Undo step. */
static int add_masters(importer *im, opr_undo *u, const int *new_from, int nnew)
{
    owf_deck *dst = im->dst;
    int total = dst->nmasters + nnew, *sm = NULL, *lm = NULL, made = 0, k, ok = 1;
    owf_master *all = calloc((size_t)total, sizeof *all);
    if (dst->nslides) sm = malloc((size_t)dst->nslides * sizeof *sm);
    if (dst->nlayouts) lm = malloc((size_t)dst->nlayouts * sizeof *lm);
    if (!all || (dst->nslides && !sm) || (dst->nlayouts && !lm))
        ok = 0;
    for (k = 0; ok && k < total; k++) {
        made = k + 1;
        if (k < dst->nmasters ? copy_own_master(&all[k], &dst->masters[k]) < 0
                              : copy_master(im, &all[k], &im->src->masters[new_from[k - dst->nmasters]]) < 0)
            ok = 0;
    }
    if (ok) {
        for (k = 0; k < dst->nslides; k++) sm[k] = dst->slides[k].master;
        for (k = 0; k < dst->nlayouts; k++) lm[k] = dst->layouts[k].master;
        ok = opr_masters_set(u, dst, all, total, total, sm, lm) == 0;
    }
    if (!ok)
        free_masters(all, made);
    free(sm);
    free(lm);
    return ok ? 0 : -1;
}

int opr_slides_insert_from(opr_undo *u, owf_deck *dst, int at, const owf_deck *src, const int *slides, int n,
                           int *selected, int *left_out)
{
    importer im;
    int *mmap = NULL, *new_from = NULL, nnew = 0, i, rc = -1, grouped = 0, index;
    if (left_out) *left_out = 0;
    if (!u || !dst || !src || src == dst || n < 1 || !src->nmasters || !dst->res || !slides)
        return -1;
    for (i = 0; i < n; i++)
        if (slides[i] < 0 || slides[i] >= src->nslides)
            return -1;
    memset(&im, 0, sizeof im);
    im.dst = dst;
    im.src = src;
    im.next_id = owf_deck_new_obj_id(dst);
    if (src->res && src->res->nimages) {
        im.images = malloc((size_t)src->res->nimages * sizeof *im.images);
        if (!im.images)
            return -1;
        for (i = 0; i < src->res->nimages; i++)
            im.images[i] = -2;
    }
    mmap = malloc((size_t)src->nmasters * sizeof *mmap);
    new_from = malloc((size_t)src->nmasters * sizeof *new_from);
    if (!mmap || !new_from)
        goto out;
    for (i = 0; i < src->nmasters; i++)
        mmap[i] = -1;
    /* each master the slides use: the deck's of that name, else a new one */
    for (i = 0; i < n; i++) {
        int m = src->slides[slides[i]].master, k;
        if (m < 0 || m >= src->nmasters)
            m = 0;
        if (mmap[m] >= 0)
            continue;
        for (k = 0; k < dst->nmasters; k++)
            if (same_name(dst->masters[k].name, src->masters[m].name))
                break;
        if (k < dst->nmasters)
            mmap[m] = k;
        else {
            mmap[m] = dst->nmasters + nnew;
            new_from[nnew++] = m;
        }
    }
    if (opr_undo_group_begin(u, n + 1) < 0)
        goto out;
    grouped = 1;
    if (nnew && add_masters(&im, u, new_from, nnew) < 0)
        goto out;
    index = at < 0 || at > dst->nslides ? dst->nslides : at;
    for (i = 0; i < n; i++) {
        const owf_slide *from = &src->slides[slides[i]];
        int m = from->master >= 0 && from->master < src->nmasters ? from->master : 0;
        owf_slide copy;
        if (copy_slide(&im, &copy, from, mmap[m], left_out) < 0)
            goto out;
        if (opr_slide_insert_owned(u, dst, index + i, &copy, NULL) < 0) {
            slide_free(&copy);
            goto out;
        }
    }
    grouped = 0;
    if (opr_undo_group_end(u) < 0)
        goto out;
    if (selected) *selected = index;
    rc = 0;
out:
    if (grouped)
        opr_undo_group_cancel(u, dst);
    free(im.images);
    free(mmap);
    free(new_from);
    return rc;
}

/* ---- The sorter's slide clipboard: a deck of its own ---- */

struct opr_clip {
    owf_deck *deck;
};

opr_clip *opr_clip_new(void)
{
    return calloc(1, sizeof(opr_clip));
}

void opr_clip_free(opr_clip *c)
{
    if (!c)
        return;
    owf_deck_free(c->deck);
    free(c);
}

int opr_clip_count(const opr_clip *c)
{
    return c && c->deck ? c->deck->nslides : 0;
}

int opr_clip_copy(opr_clip *c, const owf_deck *deck, const int *slides, int n)
{
    owf_deck *d;
    opr_undo *u;
    int rc;
    if (!c || !deck || n < 1)
        return -1;
    d = owf_deck_new_empty();
    u = opr_undo_new();
    if (!d || !u) {
        owf_deck_free(d);
        opr_undo_free(u);
        return -1;
    }
    d->width = deck->width;
    d->height = deck->height;
    rc = opr_slides_insert_from(u, d, 0, deck, slides, n, NULL, NULL);
    opr_undo_free(u);
    if (rc < 0) {
        owf_deck_free(d);
        return -1;
    }
    owf_deck_free(c->deck);
    c->deck = d;
    return 0;
}

int opr_clip_paste(const opr_clip *c, opr_undo *u, owf_deck *deck, int at, int *selected)
{
    int *all, i, rc;
    if (!c || !c->deck || !c->deck->nslides)
        return -1;
    all = malloc((size_t)c->deck->nslides * sizeof *all);
    if (!all)
        return -1;
    for (i = 0; i < c->deck->nslides; i++)
        all[i] = i;
    rc = opr_slides_insert_from(u, deck, at, c->deck, all, c->deck->nslides, selected, NULL);
    free(all);
    return rc;
}
