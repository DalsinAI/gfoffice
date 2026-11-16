/*
 * odg_clip.c: GFOpenDiag's clipboard on the editing core: Copy, Cut, Paste and Duplicate. See
 * include/opendiag_clip.h. Written for GFOpenDiag; no other program's code.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "opendiag_clip.h"

#include <stdio.h>
#include <string.h>

#define MAX_DEPTH 64

void odg_clip_init(odg_clip *clip)
{
    memset(clip, 0, sizeof *clip);
}

void odg_clip_free(odg_clip *clip)
{
    owf_dpage_free(&clip->cells);
    memset(clip, 0, sizeof *clip);
}

int odg_clip_empty(const odg_clip *clip)
{
    return clip->cells.ncells == 0;
}

/* A root or a layer: no parent, or a parent with none. */
static int is_layer_or_root(const owf_dpage *p, const owf_dcell *c)
{
    const owf_dcell *parent = owf_dpage_find(p, c->parent);
    return !c->parent || !parent || !parent->parent;
}

/* 1 when a cell the id names, or one it is inside, is marked. */
static int under_marked(const owf_dpage *p, const unsigned char *mark, const char *id)
{
    int depth = 0, i;
    while (id && depth++ <= MAX_DEPTH) {
        i = owf_dpage_index(p, id);
        if (i < 0)
            return 0;
        if (mark[i])
            return 1;
        id = p->cells[i].parent;
    }
    return 0;
}

static int in_set(const owf_dpage *p, const unsigned char *in, const char *id)
{
    int i = owf_dpage_index(p, id);
    return i >= 0 && in[i];
}

/* Where a vertex's places are measured from: its corner on the page; 0, 0 for a layer or the root. */
static void origin_of(const owf_dpage *p, const char *id, owf_dnum *x, owf_dnum *y)
{
    const owf_dcell *c = owf_dpage_find(p, id);
    owf_dnum box[4];
    *x = *y = 0;
    if (c && c->kind == OWF_DCELL_VERTEX && owf_dcell_box(p, c, box)) {
        *x = box[0];
        *y = box[1];
    }
}

static void shift_edge(owf_dcell *e, owf_dnum dx, owf_dnum dy)
{
    int k;
    for (k = 0; k < e->geo.npoints; k++) {
        e->geo.points[k].x += dx;
        e->geo.points[k].y += dy;
    }
    e->geo.source_point.x += dx;
    e->geo.source_point.y += dy;
    e->geo.target_point.x += dx;
    e->geo.target_point.y += dy;
}

/* An end (0 the source, 1 the target) of the copy e made loose at at, a place on the page; ox, oy is where e's
 * places are measured from. */
static int loosen(owf_dcell *e, int end, owf_dpoint at, owf_dnum ox, owf_dnum oy)
{
    at.x -= ox;
    at.y -= oy;
    if (end) {
        e->geo.flags |= OWF_DGEO_TARGET_POINT;
        e->geo.target_point = at;
    } else {
        e->geo.flags |= OWF_DGEO_SOURCE_POINT;
        e->geo.source_point = at;
    }
    if (owf_dcell_set(end ? &e->target : &e->source, NULL) != OWF_OK ||
        owf_dstyle_set(&e->style, end ? "entryX" : "exitX", NULL) != OWF_OK ||
        owf_dstyle_set(&e->style, end ? "entryY" : "exitY", NULL) != OWF_OK)
        return OWF_ERR_MEMORY;
    return OWF_OK;
}

/* Which cells go: in[i] 1 for each. Returns how many. */
static int choose(const owf_dpage *p, const char *const *ids, int n, unsigned char *sel, unsigned char *in)
{
    int i, count = 0, more;
    for (i = 0; i < n; i++) {
        int k = owf_dpage_index(p, ids[i]);
        if (k >= 0 && (p->cells[k].kind == OWF_DCELL_VERTEX || p->cells[k].kind == OWF_DCELL_EDGE) &&
            !is_layer_or_root(p, &p->cells[k]))
            sel[k] = 1;
    }
    for (i = 0; i < p->ncells; i++)      /* the cells named, and every cell inside them */
        in[i] = !is_layer_or_root(p, &p->cells[i]) && under_marked(p, sel, p->cells[i].id);
    do {
        more = 0;
        for (i = 0; i < p->ncells; i++) {
            const owf_dcell *c = &p->cells[i];
            const owf_dcell *parent = owf_dpage_find(p, c->parent);
            if (in[i] && parent && parent->kind == OWF_DCELL_EDGE && !in_set(p, in, c->parent)) {
                in[i] = 0;               /* a connector's label goes only with its connector */
                more = 1;
            } else if (!in[i] && c->kind == OWF_DCELL_EDGE && c->source && c->target &&
                       in_set(p, in, c->source) && in_set(p, in, c->target) && !is_layer_or_root(p, c)) {
                in[i] = 1;               /* a connector between two copied shapes comes along */
                more = 1;
            } else if (!in[i] && parent && parent->kind == OWF_DCELL_EDGE && in_set(p, in, c->parent)) {
                in[i] = 1;               /* and so does its label */
                more = 1;
            }
        }
    } while (more);
    for (i = 0; i < p->ncells; i++)
        count += in[i];
    return count;
}

/* The copied cells into out (empty). */
static int copy_cells(const owf_dpage *p, const unsigned char *in, owf_dpage *out)
{
    int i;
    for (i = 0; i < p->ncells; i++) {
        const owf_dcell *c = &p->cells[i];
        owf_dcell *k;
        int keep_parent;
        owf_dnum ox, oy;
        if (!in[i])
            continue;
        keep_parent = in_set(p, in, c->parent);
        if (!(k = owf_dpage_add_copy(out, c, c->id)))
            return OWF_ERR_MEMORY;
        if (!keep_parent) {              /* a top cell: its places become places on the page */
            origin_of(p, c->parent, &ox, &oy);
            if (owf_dcell_set(&k->parent, NULL) != OWF_OK)
                return OWF_ERR_MEMORY;
            if (c->kind == OWF_DCELL_EDGE) {
                shift_edge(k, ox, oy);
            } else {
                owf_dnum box[4];
                if (owf_dcell_box(p, c, box)) {
                    k->geo.x = box[0];
                    k->geo.y = box[1];
                }
                k->geo.flags &= ~OWF_DGEO_RELATIVE;
            }
        }
        if (c->kind == OWF_DCELL_EDGE && ((c->source && !in_set(p, in, c->source)) ||
                                          (c->target && !in_set(p, in, c->target)))) {
            int np = 0, r = OWF_OK;
            owf_dpoint *path = owf_dedge_path(p, c, &np);
            ox = oy = 0;
            if (keep_parent)
                origin_of(p, c->parent, &ox, &oy);
            if (!path || np < 2) {       /* an end it can't place: left out of what is copied */
                owf_free(path);
                owf_dpage_remove(out, c->id);
                continue;
            }
            if (c->source && !in_set(p, in, c->source))
                r = loosen(k, 0, path[0], ox, oy);
            if (r == OWF_OK && c->target && !in_set(p, in, c->target))
                r = loosen(k, 1, path[np - 1], ox, oy);
            owf_free(path);
            if (r != OWF_OK)
                return r;
        }
    }
    for (i = 0; i < out->ncells; i++)    /* what was inside a connector left out goes too */
        if (out->cells[i].parent && owf_dpage_index(out, out->cells[i].parent) < 0) {
            owf_dpage_remove(out, out->cells[i].id);
            i = -1;
        }
    return out->ncells ? OWF_OK : OWF_ERR_FORMAT;
}

/* The copy into a clipboard of its own (fresh): OWF_OK, OWF_ERR_FORMAT or OWF_ERR_MEMORY. */
static int copy_into(const odg_doc *doc, const char *const *ids, int n, odg_clip *fresh)
{
    const owf_dpage *p = odg_page(doc);
    unsigned char *sel, *in;
    int r;
    odg_clip_init(fresh);
    if (n <= 0 || !ids)
        return OWF_ERR_FORMAT;
    sel = owf_calloc((size_t)p->ncells + 1, 1);
    in = owf_calloc((size_t)p->ncells + 1, 1);
    if (!sel || !in)
        r = OWF_ERR_MEMORY;
    else if (!choose(p, ids, n, sel, in))
        r = OWF_ERR_FORMAT;
    else
        r = copy_cells(p, in, &fresh->cells);
    owf_free(sel);
    owf_free(in);
    if (r != OWF_OK)
        odg_clip_free(fresh);
    return r;
}

int odg_copy(const odg_doc *doc, const char *const *ids, int n, odg_clip *clip)
{
    odg_clip fresh;
    int r = copy_into(doc, ids, n, &fresh);
    if (r != OWF_OK)
        return r;
    odg_clip_free(clip);
    *clip = fresh;
    clip->next = 1;
    return OWF_OK;
}

int odg_cut(odg_doc *doc, const char *const *ids, int n, odg_clip *clip)
{
    odg_clip fresh;
    const char **tops;
    int r = copy_into(doc, ids, n, &fresh), i, ntops = 0;
    if (r != OWF_OK)
        return r;
    tops = owf_malloc((size_t)fresh.cells.ncells * sizeof *tops);
    if (!tops) {
        odg_clip_free(&fresh);
        return OWF_ERR_MEMORY;
    }
    for (i = 0; i < fresh.cells.ncells; i++)
        if (!fresh.cells.cells[i].parent)
            tops[ntops++] = fresh.cells.cells[i].id;
    r = odg_delete(doc, tops, ntops);
    owf_free(tops);
    if (r != OWF_OK) {
        odg_clip_free(&fresh);
        return r;
    }
    odg_clip_free(clip);
    *clip = fresh;
    clip->next = 0;
    return OWF_OK;
}

/* The layer pasted cells go on: the current one when it is shown and not locked; NULL when not. */
static const char *paste_layer(const odg_doc *doc)
{
    int i = odg_layer_current(doc);
    if (i < 0 || !odg_layer_shown(doc, i) || odg_layer_locked(doc, i))
        return NULL;
    return odg_layer_at(doc, i)->id;
}

/* The new id of the clipboard's cell with id old (cells base.. on p are the clipboard's, in its order). */
static const char *new_id(const owf_dpage *p, int base, const owf_dpage *clip, const char *old)
{
    int k = owf_dpage_index(clip, old);
    return k >= 0 ? p->cells[base + k].id : NULL;
}

static int put_cells(odg_doc *doc, const odg_clip *clip, owf_dnum dx, owf_dnum dy, char (*made)[ODG_CLIP_ID],
                     int max, int *count)
{
    const owf_dpage *cp = &clip->cells;
    owf_dpage *p = odg_page(doc);
    char layer[ODG_CLIP_ID * 4];
    int base = p->ncells, k;

    snprintf(layer, sizeof layer, "%s", paste_layer(doc));
    for (k = 0; k < cp->ncells; k++)
        if (!owf_dpage_add_copy(p, &cp->cells[k], NULL))
            return OWF_ERR_MEMORY;
    for (k = 0; k < cp->ncells; k++) {
        const owf_dcell *from = &cp->cells[k];
        owf_dcell *c = &p->cells[base + k];
        const char *parent = from->parent ? new_id(p, base, cp, from->parent) : NULL;
        if (owf_dcell_set(&c->parent, parent ? parent : layer) != OWF_OK ||
            owf_dcell_set(&c->source, from->source ? new_id(p, base, cp, from->source) : NULL) != OWF_OK ||
            owf_dcell_set(&c->target, from->target ? new_id(p, base, cp, from->target) : NULL) != OWF_OK)
            return OWF_ERR_MEMORY;
        if (parent)
            continue;
        if (c->kind == OWF_DCELL_EDGE)
            shift_edge(c, dx, dy);
        else {
            c->geo.x += dx;
            c->geo.y += dy;
        }
        if (made && *count < max)
            snprintf(made[*count], ODG_CLIP_ID, "%s", c->id);
        (*count)++;
    }
    return OWF_OK;
}

int odg_paste(odg_doc *doc, odg_clip *clip, owf_dnum dx, owf_dnum dy, char (*made)[ODG_CLIP_ID], int max,
              int *nmade)
{
    const char *layer = paste_layer(doc);
    int count = 0, r;
    if (nmade)
        *nmade = 0;
    if (odg_clip_empty(clip) || !layer || strlen(layer) >= ODG_CLIP_ID * 4)
        return OWF_ERR_FORMAT;
    if (odg_begin(doc) != OWF_OK)
        return OWF_ERR_MEMORY;
    r = odg_end(doc, put_cells(doc, clip, dx, dy, made, max, &count));
    if (r == OWF_OK && nmade)
        *nmade = count;
    return r;
}

int odg_paste_next(odg_doc *doc, odg_clip *clip, char (*made)[ODG_CLIP_ID], int max, int *nmade)
{
    owf_dnum step = (owf_dnum)clip->next * ODG_CLIP_STEP;
    int r = odg_paste(doc, clip, step, step, made, max, nmade);
    if (r == OWF_OK)
        clip->next++;
    return r;
}

int odg_duplicate(odg_doc *doc, const char *const *ids, int n, owf_dnum dx, owf_dnum dy, char (*made)[ODG_CLIP_ID],
                  int max, int *nmade)
{
    odg_clip fresh;
    int r = copy_into(doc, ids, n, &fresh);
    if (nmade)
        *nmade = 0;
    if (r != OWF_OK)
        return r;
    r = odg_paste(doc, &fresh, dx, dy, made, max, nmade);
    odg_clip_free(&fresh);
    return r;
}
