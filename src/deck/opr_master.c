/* GFOpenPresentation core: the Master view (Impress parity P7).
 *
 * The masters are edited as slides. opr_master_view_open makes a deck whose
 * slide i is master i: the master's art and placeholders are the slide's
 * objects and its background the slide's, under a copy of the master that
 * keeps its text levels and has no objects. That deck shares the real deck's
 * pictures, fonts and kept XML, so every slide command (move, resize, type,
 * insert a picture, delete, Undo, new, duplicate, rename, delete a slide)
 * works on the masters unchanged. opr_master_view_commit writes the slides
 * back as the deck's masters in one Undo step of the real deck: a slide that
 * was master i stays master i's (its slides follow it wherever it moved), a
 * new one is a new master, and the slides of a master that was deleted move
 * to the first master.
 *
 * Which master a slide of the view was is kept in its keep_timing as
 * -2 - index: the view is never saved, and a new or duplicated slide's -1
 * says it is a new master. */

#include <stdlib.h>
#include <string.h>

#include "openpresentation_core.h"

struct opr_master_view {
    owf_deck *view;
    owf_deck *deck;              /* what it borrows from */
};

static char *copy_string(const char *s)
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

/* A master without its objects: name, background and text levels. */
static int master_shell(owf_master *to, const owf_master *from)
{
    *to = *from;
    to->objs = NULL;
    to->nobjs = to->capobjs = 0;
    to->name = copy_string(from->name);
    return !from->name || to->name;
}

static int copy_objs(owf_obj **to, int *n, int *cap, const owf_obj *from, int count)
{
    int i;
    *to = NULL;
    *n = *cap = 0;
    if (count <= 0)
        return 1;
    *to = calloc((size_t)count, sizeof **to);
    if (!*to)
        return 0;
    *cap = count;
    for (i = 0; i < count; i++) {
        if (owf_obj_copy(&(*to)[i], &from[i]) != 0) {
            while (i-- > 0)
                owf_obj_clear(&(*to)[i]);
            free(*to);
            *to = NULL;
            *cap = 0;
            return 0;
        }
    }
    *n = count;
    return 1;
}

static void free_masters(owf_master *masters, int n)
{
    int i, k;
    if (!masters)
        return;
    for (i = 0; i < n; i++) {
        for (k = 0; k < masters[i].nobjs; k++)
            owf_obj_clear(&masters[i].objs[k]);
        free(masters[i].objs);
        free(masters[i].name);
    }
    free(masters);
}

static void lend(owf_deck *view, const owf_deck *deck)
{
    view->res = deck->res;
    view->kept = deck->kept;
    view->nkept = deck->nkept;
    view->capkept = deck->capkept;
    view->kept_format = deck->kept_format;
    view->parts = deck->parts;
    view->nparts = deck->nparts;
    view->capparts = deck->capparts;
    view->kept_bytes = deck->kept_bytes;
}

/* What the view borrowed goes back (a picture inserted grew res itself). */
static void take_back(owf_deck *view, owf_deck *deck)
{
    deck->kept = view->kept;
    deck->nkept = view->nkept;
    deck->capkept = view->capkept;
    deck->parts = view->parts;
    deck->nparts = view->nparts;
    deck->capparts = view->capparts;
    deck->kept_bytes = view->kept_bytes;
    view->res = NULL;
    view->kept = NULL;
    view->nkept = view->capkept = 0;
    view->parts = NULL;
    view->nparts = view->capparts = 0;
    view->kept_bytes = 0;
}

opr_master_view *opr_master_view_open(owf_deck *deck)
{
    opr_master_view *v;
    owf_deck *w;
    int i;
    if (!deck || deck->nmasters < 1)
        return NULL;
    v = calloc(1, sizeof *v);
    if (!v)
        return NULL;
    v->deck = deck;
    w = v->view = owf_deck_new_empty();
    if (!w) {
        free(v);
        return NULL;
    }
    w->width = deck->width;
    w->height = deck->height;
    w->notes_page = deck->notes_page;
    owf_doc_free(w->res);        /* its own empty one: the deck's is lent */
    w->res = NULL;
    lend(w, deck);
    w->masters = calloc((size_t)deck->nmasters, sizeof *w->masters);
    w->slides = calloc((size_t)deck->nmasters, sizeof *w->slides);
    if (!w->masters || !w->slides)
        goto fail;
    w->capmasters = w->capslides = deck->nmasters;
    for (i = 0; i < deck->nmasters; i++) {
        const owf_master *m = &deck->masters[i];
        owf_slide *s = &w->slides[i];
        if (!master_shell(&w->masters[i], m))
            goto fail;
        w->nmasters = i + 1;
        memset(s, 0, sizeof *s);
        s->master = i;
        s->layout = -1;
        s->background = m->background;
        s->background_image = m->background_image;
        s->keep_timing = -2 - i;
        owf_obj_init(&s->notes, OWF_OBJ_TEXT);
        s->notes.placeholder = OWF_PH_NOTES;
        s->transition.keep = -1;
        s->transition.advance_after = -1;
        s->name = copy_string(m->name);
        w->nslides = i + 1;
        if ((m->name && !s->name) || !copy_objs(&s->objs, &s->nobjs, &s->capobjs, m->objs, m->nobjs))
            goto fail;
    }
    return v;
fail:
    opr_master_view_free(v);
    return NULL;
}

owf_deck *opr_master_view_deck(opr_master_view *v)
{
    return v ? v->view : NULL;
}

int opr_master_view_origin(const opr_master_view *v, int slide)
{
    int k;
    if (!v || slide < 0 || slide >= v->view->nslides)
        return -1;
    k = v->view->slides[slide].keep_timing;
    return k <= -2 ? -2 - k : -1;
}

void opr_master_view_free(opr_master_view *v)
{
    if (!v)
        return;
    if (v->view) {
        take_back(v->view, v->deck);
        owf_deck_free(v->view);
    }
    free(v);
}

int opr_master_view_commit(opr_master_view *v, opr_undo *u)
{
    owf_deck *view, *deck;
    owf_master *masters = NULL;
    int *slide_masters = NULL, *layout_masters = NULL, *where = NULL;
    int n, i, made = 0;
    if (!v || !u)
        return -1;
    view = v->view;
    deck = v->deck;
    n = view->nslides;
    if (n < 1)
        return -2;                       /* a deck keeps one master at least */
    masters = calloc((size_t)n, sizeof *masters);
    where = malloc((size_t)deck->nmasters * sizeof *where);
    if (deck->nslides)
        slide_masters = malloc((size_t)deck->nslides * sizeof *slide_masters);
    if (deck->nlayouts)
        layout_masters = malloc((size_t)deck->nlayouts * sizeof *layout_masters);
    if (!masters || !where || (deck->nslides && !slide_masters) || (deck->nlayouts && !layout_masters))
        goto fail;
    for (i = 0; i < deck->nmasters; i++)
        where[i] = -1;
    for (i = 0; i < n; i++) {
        const owf_slide *s = &view->slides[i];
        const owf_master *shell = &view->masters[s->master >= 0 && s->master < view->nmasters ? s->master : 0];
        int origin = opr_master_view_origin(v, i);
        if (!master_shell(&masters[i], shell))
            goto fail;
        made = i + 1;
        free(masters[i].name);
        masters[i].name = copy_string(s->name);
        if (s->name && !masters[i].name)
            goto fail;
        masters[i].background = s->background;
        masters[i].background_image = s->background_image;
        if (!copy_objs(&masters[i].objs, &masters[i].nobjs, &masters[i].capobjs, s->objs, s->nobjs))
            goto fail;
        if (origin >= 0 && origin < deck->nmasters && where[origin] < 0)
            where[origin] = i;           /* a duplicate's copy is a new master */
    }
    for (i = 0; i < deck->nslides; i++) {
        int m = deck->slides[i].master;
        slide_masters[i] = m >= 0 && m < deck->nmasters && where[m] >= 0 ? where[m] : 0;
    }
    for (i = 0; i < deck->nlayouts; i++) {
        int m = deck->layouts[i].master;
        layout_masters[i] = m < 0 ? -1 : (m < deck->nmasters && where[m] >= 0 ? where[m] : 0);
    }
    if (opr_masters_set(u, deck, masters, n, n, slide_masters, layout_masters) != 0)
        goto fail;
    /* the view's slides are the masters now: each one is its own origin */
    for (i = 0; i < n; i++)
        view->slides[i].keep_timing = -2 - i;
    free(where);
    free(slide_masters);
    free(layout_masters);
    return 0;
fail:
    free_masters(masters, made);
    free(where);
    free(slide_masters);
    free(layout_masters);
    return -1;
}

int opr_master_slides(const owf_deck *deck, int master)
{
    int i, n = 0;
    if (!deck)
        return 0;
    for (i = 0; i < deck->nslides; i++)
        if (deck->slides[i].master == master)
            n++;
    return n;
}
