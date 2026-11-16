/*
 * odg_align.c: GFOpenDiag's Align and Distribute on the editing core. See include/opendiag_align.h.
 * Written for GFOpenDiag; no other program's code. Integer arithmetic only.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "opendiag_align.h"

#include <string.h>

#define MAX_DEPTH 64

typedef long long wide;

typedef struct {
    const char *id;                      /* the cell's own id string: it stays the same across odg_begin */
    owf_dnum box[4];                     /* on the page */
    owf_dnum dx, dy;                     /* how far it is to move */
} item;

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

/* The shapes that move, in page order, into items (room for n). Returns how many; -1 when memory ran out. */
static int gather(const owf_dpage *p, const char *const *ids, int n, item *items)
{
    unsigned char *named = owf_calloc((size_t)p->ncells + 1, 1);
    int i, k, count = 0;
    if (!named)
        return -1;
    for (i = 0; i < n; i++) {
        k = owf_dpage_index(p, ids[i]);
        if (k >= 0) {
            const owf_dcell *c = &p->cells[k];
            const owf_dcell *parent = owf_dpage_find(p, c->parent);
            if (c->kind == OWF_DCELL_VERTEX && !is_layer_or_root(p, c) && !(c->geo.flags & OWF_DGEO_RELATIVE) &&
                !(parent && parent->kind == OWF_DCELL_EDGE))
                named[k] = 1;
        }
    }
    for (k = 0; k < p->ncells && count < n; k++) {
        const owf_dcell *c = &p->cells[k];
        if (named[k] && !under_marked(p, named, c->parent) && owf_dcell_box(p, c, items[count].box)) {
            items[count].id = c->id;
            items[count].dx = items[count].dy = 0;
            count++;
        }
    }
    owf_free(named);
    return count;
}

static int is_orthogonal(const owf_dcell *e)
{
    char v[32];
    return owf_dstyle_get(e->style, "edgeStyle", v, sizeof v) && !strcmp(v, "orthogonalEdgeStyle");
}

/* The moves done, as one Undo step, with the orthogonal connectors joined to a moved shape routed again. */
static int apply(odg_doc *doc, const item *items, int count)
{
    owf_dpage *p;
    unsigned char *mark;
    int i, r = OWF_OK, any = 0;
    for (i = 0; i < count; i++)
        any |= items[i].dx != 0 || items[i].dy != 0;
    if (!any)
        return OWF_ERR_FORMAT;
    if (odg_begin(doc) != OWF_OK)
        return OWF_ERR_MEMORY;
    p = odg_page(doc);
    mark = owf_calloc((size_t)p->ncells + 1, 1);
    if (!mark)
        return odg_end(doc, OWF_ERR_MEMORY);
    for (i = 0; i < count; i++) {
        int k = owf_dpage_index(p, items[i].id);
        if (k < 0)
            continue;
        p->cells[k].geo.x += items[i].dx;
        p->cells[k].geo.y += items[i].dy;
        mark[k] = items[i].dx != 0 || items[i].dy != 0;
    }
    for (i = 0; i < p->ncells && r == OWF_OK; i++) {
        const owf_dcell *e = &p->cells[i];
        if (e->kind == OWF_DCELL_EDGE && is_orthogonal(e) &&
            (under_marked(p, mark, e->source) || under_marked(p, mark, e->target))) {
            r = odg_route(doc, e->id);
            if (r == OWF_ERR_FORMAT)
                r = OWF_OK;              /* an end that isn't anywhere: left as it is */
            p = odg_page(doc);
        }
    }
    owf_free(mark);
    return odg_end(doc, r);
}

int odg_align(odg_doc *doc, const char *const *ids, int n, odg_align_how how)
{
    item *items;
    owf_dnum lo = 0, hi = 0, mid;
    int count, i, r, across = how <= ODG_ALIGN_RIGHT, a = across ? 0 : 1;

    if (n < 2 || !ids || how < ODG_ALIGN_LEFT || how > ODG_ALIGN_BOTTOM)
        return OWF_ERR_FORMAT;
    items = owf_malloc((size_t)n * sizeof *items);
    if (!items)
        return OWF_ERR_MEMORY;
    count = gather(odg_page(doc), ids, n, items);
    if (count < 2) {
        owf_free(items);
        return count < 0 ? OWF_ERR_MEMORY : OWF_ERR_FORMAT;
    }
    for (i = 0; i < count; i++) {        /* the box round them all, the one way */
        owf_dnum s = items[i].box[a], e = s + items[i].box[a + 2];
        if (!i || s < lo)
            lo = s;
        if (!i || e > hi)
            hi = e;
    }
    mid = (owf_dnum)(((wide)lo + hi) / 2);
    for (i = 0; i < count; i++) {
        owf_dnum s = items[i].box[a], size = items[i].box[a + 2], d = 0;
        switch (how) {
        case ODG_ALIGN_LEFT: case ODG_ALIGN_TOP: d = lo - s; break;
        case ODG_ALIGN_RIGHT: case ODG_ALIGN_BOTTOM: d = hi - (s + size); break;
        default: d = mid - (s + size / 2); break;
        }
        if (across)
            items[i].dx = d;
        else
            items[i].dy = d;
    }
    r = apply(doc, items, count);
    owf_free(items);
    return r;
}

static owf_dnum centre(const item *it, int a)
{
    return it->box[a] + it->box[a + 2] / 2;
}

int odg_distribute(odg_doc *doc, const char *const *ids, int n, int across)
{
    item *items;
    int count, i, j, r, a = across ? 0 : 1;
    owf_dnum first, last;

    if (n < 3 || !ids)
        return OWF_ERR_FORMAT;
    items = owf_malloc((size_t)n * sizeof *items);
    if (!items)
        return OWF_ERR_MEMORY;
    count = gather(odg_page(doc), ids, n, items);
    if (count < 3) {
        owf_free(items);
        return count < 0 ? OWF_ERR_MEMORY : OWF_ERR_FORMAT;
    }
    for (i = 1; i < count; i++) {        /* by their middles; ones level keep the page's order */
        item t = items[i];
        for (j = i; j > 0 && centre(&items[j - 1], a) > centre(&t, a); j--)
            items[j] = items[j - 1];
        items[j] = t;
    }
    first = centre(&items[0], a);
    last = centre(&items[count - 1], a);
    for (i = 1; i < count - 1; i++) {
        owf_dnum want = (owf_dnum)(first + (wide)(last - first) * i / (count - 1));
        owf_dnum d = want - centre(&items[i], a);
        if (across)
            items[i].dx = d;
        else
            items[i].dy = d;
    }
    r = apply(doc, items, count);
    owf_free(items);
    return r;
}
