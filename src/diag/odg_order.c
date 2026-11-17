/*
 * odg_order.c: GFOpenDiag's drawing order and Group and Ungroup on the editing core. See include/opendiag_order.h.
 * The group style word "group" and connectable="0" are draw.io's names in its files, so groups made here open as
 * groups in diagrams.net. Written for GFOpenDiag; no other program's code.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "opendiag_order.h"

#include <stdio.h>
#include <string.h>

#define MAX_DEPTH 64

static int is_layer_or_root(const owf_dpage *p, const owf_dcell *c)
{
    const owf_dcell *parent = owf_dpage_find(p, c->parent);
    return !c->parent || !parent || !parent->parent;
}

/* A shape or connector that can be arranged: not the root, a layer, or a label along a connector. */
static int arrangeable(const owf_dpage *p, const owf_dcell *c)
{
    const owf_dcell *parent;
    if (!c || (c->kind != OWF_DCELL_VERTEX && c->kind != OWF_DCELL_EDGE) || is_layer_or_root(p, c))
        return 0;
    parent = owf_dpage_find(p, c->parent);
    return !(parent && parent->kind == OWF_DCELL_EDGE);
}

int odg_is_group(const owf_dcell *c)
{
    return c && c->kind == OWF_DCELL_VERTEX && owf_dstyle_is(c->style, "group");
}

/* 1 when cell i is the cell with id root or inside it. */
static int under(const owf_dpage *p, int i, const char *root)
{
    const char *id = p->cells[i].id;
    int depth = 0;
    while (id && depth++ <= MAX_DEPTH) {
        int k;
        if (!strcmp(id, root))
            return 1;
        k = owf_dpage_index(p, id);
        if (k < 0)
            return 0;
        id = p->cells[k].parent;
    }
    return 0;
}

static int same(const char *x, const char *y)
{
    return x && y && !strcmp(x, y);
}

/* ---- Moving blocks of cells in the drawing order ---- */

typedef struct {
    owf_dcell *v;
    int n;
} block;

/* The cells marked taken out of the page, in their order, into b. */
static int take(owf_dpage *p, const unsigned char *mark, block *b)
{
    int i, j = 0;
    b->n = 0;
    b->v = owf_malloc((size_t)p->ncells * sizeof *b->v + 1);
    if (!b->v)
        return OWF_ERR_MEMORY;
    for (i = 0; i < p->ncells; i++) {
        if (mark[i])
            b->v[b->n++] = p->cells[i];
        else
            p->cells[j++] = p->cells[i];
    }
    p->ncells = j;
    return OWF_OK;
}

/* And put back before cell at (at == ncells: at the end). There is room: the cells came out of the page. */
static void put(owf_dpage *p, block *b, int at)
{
    memmove(p->cells + at + b->n, p->cells + at, (size_t)(p->ncells - at) * sizeof *p->cells);
    memcpy(p->cells + at, b->v, (size_t)b->n * sizeof *p->cells);
    p->ncells += b->n;
    owf_free(b->v);
    b->v = NULL;
}

/* The first and last index of the cells with id root and inside it; 0 when there are none. */
static int span(const owf_dpage *p, const char *root, int *first, int *last)
{
    int i, any = 0;
    for (i = 0; i < p->ncells; i++)
        if (under(p, i, root)) {
            if (!any)
                *first = i;
            *last = i;
            any = 1;
        }
    return any;
}

/* Cell id (with what is inside it) moved one way in the drawing order. */
static int move_one(owf_dpage *p, const char *id, odg_order how)
{
    unsigned char *mark;
    const char *parent;
    block b;
    int i, at = -1, pos = 0, first = 0, last = 0, r;

    i = owf_dpage_index(p, id);
    if (i < 0)
        return OWF_OK;
    parent = p->cells[i].parent;
    mark = owf_calloc((size_t)p->ncells + 1, 1);
    if (!mark)
        return OWF_ERR_MEMORY;
    for (i = 0; i < p->ncells; i++)
        mark[i] = (unsigned char)under(p, i, id);
    for (i = 0; i < p->ncells && !mark[i]; i++)
        pos++;                           /* where the block starts, counted in the cells left */
    r = take(p, mark, &b);
    owf_free(mark);
    if (r != OWF_OK)
        return r;
    switch (how) {
    case ODG_ORDER_FRONT:                /* after the last cell inside the parent */
        at = span(p, parent, &first, &last) ? last + 1 : p->ncells;
        break;
    case ODG_ORDER_BACK:                 /* before the first cell beside it */
        for (i = 0; i < p->ncells && at < 0; i++)
            if (same(p->cells[i].parent, parent) && span(p, p->cells[i].id, &first, &last))
                at = first;
        break;
    case ODG_ORDER_FORWARD:              /* after the next cell beside it, and what is inside that */
        for (i = pos; i < p->ncells && at < 0; i++)
            if (same(p->cells[i].parent, parent) && span(p, p->cells[i].id, &first, &last))
                at = last + 1;
        break;
    case ODG_ORDER_BACKWARD:             /* before the one under it */
        for (i = pos - 1; i >= 0 && at < 0; i--)
            if (same(p->cells[i].parent, parent) && span(p, p->cells[i].id, &first, &last))
                at = first;
        break;
    }
    put(p, &b, at < 0 ? pos : at);
    return OWF_OK;
}

/* The ids of the page's cells, in order, to see afterwards whether anything moved. Cells keep their id strings
 * as they move, so the pointers are enough. */
static const char **order_of(const owf_dpage *p)
{
    const char **v = owf_malloc((size_t)p->ncells * sizeof *v + 1);
    int i;
    if (v)
        for (i = 0; i < p->ncells; i++)
            v[i] = p->cells[i].id;
    return v;
}

static int moved(const owf_dpage *p, const char **was)
{
    int i;
    for (i = 0; i < p->ncells; i++)
        if (p->cells[i].id != was[i])
            return 1;
    return 0;
}

/* The cells named that move: arrangeable, not inside another one named; marked by their index. */
static int roots(const owf_dpage *p, const char *const *ids, int n, unsigned char *root)
{
    unsigned char *named = owf_calloc((size_t)p->ncells + 1, 1);
    int i, k, count = 0;
    if (!named)
        return -1;
    for (i = 0; i < n; i++) {
        k = owf_dpage_index(p, ids[i]);
        if (k >= 0 && arrangeable(p, &p->cells[k]))
            named[k] = 1;
    }
    for (k = 0; k < p->ncells; k++) {
        const char *up = p->cells[k].parent;
        int depth = 0, inside = 0;
        if (!named[k])
            continue;
        while (up && depth++ <= MAX_DEPTH && !inside) {
            int j = owf_dpage_index(p, up);
            if (j < 0)
                break;
            inside = named[j];
            up = p->cells[j].parent;
        }
        if (!inside) {
            root[k] = 1;
            count++;
        }
    }
    owf_free(named);
    return count;
}

int odg_reorder(odg_doc *doc, const char *const *ids, int n, odg_order how)
{
    owf_dpage *p = odg_page(doc);
    unsigned char *root;
    const char **list, **was;
    int i, k, count, r = OWF_OK, up = how == ODG_ORDER_FRONT || how == ODG_ORDER_BACKWARD;

    if (n <= 0 || !ids || how < ODG_ORDER_FRONT || how > ODG_ORDER_BACKWARD)
        return OWF_ERR_FORMAT;
    root = owf_calloc((size_t)p->ncells + 1, 1);
    if (!root)
        return OWF_ERR_MEMORY;
    count = roots(p, ids, n, root);
    if (count <= 0) {
        owf_free(root);
        return count < 0 ? OWF_ERR_MEMORY : OWF_ERR_FORMAT;
    }
    list = owf_malloc((size_t)count * sizeof *list);
    if (!list) {
        owf_free(root);
        return OWF_ERR_MEMORY;
    }
    /* Front and Backward go bottom first, Back and Forward top first, so those named keep their order. */
    for (i = 0, k = 0; i < p->ncells; i++)
        if (root[i])
            list[k++] = p->cells[i].id;
    owf_free(root);
    if (odg_begin(doc) != OWF_OK) {
        owf_free(list);
        return OWF_ERR_MEMORY;
    }
    p = odg_page(doc);                   /* the same cells, in the same order: the ids in list are still theirs */
    was = order_of(p);
    if (!was)
        r = OWF_ERR_MEMORY;
    for (i = 0; i < count && r == OWF_OK; i++)
        r = move_one(p, list[up ? i : count - 1 - i], how);
    if (r == OWF_OK && !moved(p, was))
        r = OWF_ERR_FORMAT;              /* nothing moved: no Undo step */
    owf_free(was);
    owf_free(list);
    return odg_end(doc, r);
}

/* ---- Group and Ungroup ---- */

/* The corner places inside the cell with this id are measured from; 0, 0 for a layer or the root. */
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

static void shift(owf_dcell *c, owf_dnum dx, owf_dnum dy)
{
    int k;
    if (c->kind == OWF_DCELL_VERTEX) {
        if (!(c->geo.flags & OWF_DGEO_RELATIVE)) {
            c->geo.x += dx;
            c->geo.y += dy;
        }
        return;
    }
    for (k = 0; k < c->geo.npoints; k++) {
        c->geo.points[k].x += dx;
        c->geo.points[k].y += dy;
    }
    c->geo.source_point.x += dx;
    c->geo.source_point.y += dy;
    c->geo.target_point.x += dx;
    c->geo.target_point.y += dy;
}

static void grow(owf_dnum b[4], int *any, owf_dnum x0, owf_dnum y0, owf_dnum x1, owf_dnum y1)
{
    if (!*any) {
        b[0] = x0; b[1] = y0; b[2] = x1; b[3] = y1;
        *any = 1;
        return;
    }
    if (x0 < b[0]) b[0] = x0;
    if (y0 < b[1]) b[1] = y0;
    if (x1 > b[2]) b[2] = x1;
    if (y1 > b[3]) b[3] = y1;
}

/* The box round the marked cells on the page (x0, y0, x1, y1); 0 when none has a place. */
static int bounds(const owf_dpage *p, const unsigned char *mark, owf_dnum b[4])
{
    int i, any = 0;
    for (i = 0; i < p->ncells; i++) {
        const owf_dcell *c = &p->cells[i];
        owf_dnum box[4];
        if (!mark[i])
            continue;
        if (c->kind == OWF_DCELL_VERTEX) {
            if (owf_dcell_box(p, c, box))
                grow(b, &any, box[0], box[1], box[0] + box[2], box[1] + box[3]);
        } else {
            int np = 0, k;
            owf_dpoint *path = owf_dedge_path(p, c, &np);
            for (k = 0; k < np; k++)
                grow(b, &any, path[k].x, path[k].y, path[k].x, path[k].y);
            owf_free(path);
        }
    }
    return any;
}

static int make_group(owf_dpage *p, const unsigned char *member, char *out)
{
    owf_dnum b[4] = { 0, 0, 0, 0 }, ox, oy;
    const char *parent = NULL;
    char gid[ODG_ORDER_ID], par[256];
    owf_dcell *g;
    unsigned char *mark;
    block blk;
    int i, low = -1, r;

    for (i = 0; i < p->ncells; i++)
        if (member[i]) {
            if (low < 0)
                low = i;
            parent = p->cells[i].parent;
        }
    if (low < 0 || !parent || strlen(parent) >= sizeof par)
        return OWF_ERR_FORMAT;
    snprintf(par, sizeof par, "%s", parent);
    bounds(p, member, b);
    origin_of(p, par, &ox, &oy);
    owf_dpage_new_id(p, gid, sizeof gid);
    /* the members' places measured from the group's corner */
    for (i = 0; i < p->ncells; i++)
        if (member[i]) {
            const owf_dcell *c = &p->cells[i];
            owf_dnum dx = -(b[0] - ox), dy = -(b[1] - oy);
            if (c->kind == OWF_DCELL_VERTEX && !(c->geo.flags & OWF_DGEO_RELATIVE)) {
                owf_dnum box[4];
                if (owf_dcell_box(p, c, box)) {   /* from where it is on the page: exact whatever held it */
                    p->cells[i].geo.x = box[0] - b[0];
                    p->cells[i].geo.y = box[1] - b[1];
                }
            } else
                shift(&p->cells[i], dx, dy);
            if (owf_dcell_set(&p->cells[i].parent, gid) != OWF_OK)
                return OWF_ERR_MEMORY;
        }
    g = owf_dpage_add(p, gid, par, OWF_DCELL_VERTEX);
    if (!g)
        return OWF_ERR_MEMORY;
    g->geo.flags = OWF_DGEO_HAS;
    g->geo.x = b[0] - ox;
    g->geo.y = b[1] - oy;
    g->geo.width = b[2] - b[0];
    g->geo.height = b[3] - b[1];
    if (owf_dcell_set(&g->style, "group;") != OWF_OK || owf_dattrs_set(&g->extra, "connectable", "0") != OWF_OK)
        return OWF_ERR_MEMORY;
    /* the group to where the lowest member was, so they stay where they were in the drawing order */
    mark = owf_calloc((size_t)p->ncells + 1, 1);
    if (!mark)
        return OWF_ERR_MEMORY;
    mark[p->ncells - 1] = 1;
    r = take(p, mark, &blk);
    owf_free(mark);
    if (r != OWF_OK)
        return r;
    put(p, &blk, low);
    if (out)
        snprintf(out, ODG_ORDER_ID, "%s", gid);
    return OWF_OK;
}

int odg_group(odg_doc *doc, const char *const *ids, int n, char *group)
{
    owf_dpage *p = odg_page(doc);
    unsigned char *member;
    const char *parent = NULL;
    int i, k, count = 0;

    if (n <= 0 || !ids)
        return OWF_ERR_FORMAT;
    for (i = 0; i < n && !parent; i++) {
        const owf_dcell *c = owf_dpage_find(p, ids[i]);
        if (arrangeable(p, c))
            parent = c->parent;
    }
    if (!parent)
        return OWF_ERR_FORMAT;
    if (odg_begin(doc) != OWF_OK)
        return OWF_ERR_MEMORY;
    p = odg_page(doc);
    member = owf_calloc((size_t)p->ncells + 1, 1);
    if (!member)
        return odg_end(doc, OWF_ERR_MEMORY);
    for (i = 0; i < n; i++) {
        k = owf_dpage_index(p, ids[i]);
        if (k >= 0 && !member[k] && arrangeable(p, &p->cells[k]) && same(p->cells[k].parent, parent)) {
            member[k] = 1;
            count++;
        }
    }
    k = count ? make_group(p, member, group) : OWF_ERR_FORMAT;
    owf_free(member);
    return odg_end(doc, k);
}

/* Connectors joined to the group with this id (draw.io makes groups connectable="0", but a file may join one) keep
 * that end loose where it is, so that removing the group doesn't take them with it. */
static int loosen_joined(owf_dpage *p, const char *gid)
{
    int i, end;
    for (i = 0; i < p->ncells; i++) {
        owf_dcell *e = &p->cells[i];
        owf_dpoint *path;
        owf_dnum ox, oy;
        int np = 0;
        if (e->kind != OWF_DCELL_EDGE || !(same(e->source, gid) || same(e->target, gid)))
            continue;
        path = owf_dedge_path(p, e, &np);
        if (!path || np < 2) {
            owf_free(path);
            continue;                    /* nowhere to leave it: it goes with the group */
        }
        origin_of(p, e->parent, &ox, &oy);
        for (end = 0; end < 2; end++) {
            owf_dpoint at = path[end ? np - 1 : 0];
            if (!same(end ? e->target : e->source, gid))
                continue;
            at.x -= ox;
            at.y -= oy;
            e->geo.flags |= end ? OWF_DGEO_TARGET_POINT : OWF_DGEO_SOURCE_POINT;
            if (end)
                e->geo.target_point = at;
            else
                e->geo.source_point = at;
            if (owf_dcell_set(end ? &e->target : &e->source, NULL) != OWF_OK ||
                owf_dstyle_set(&e->style, end ? "entryX" : "exitX", NULL) != OWF_OK ||
                owf_dstyle_set(&e->style, end ? "entryY" : "exitY", NULL) != OWF_OK) {
                owf_free(path);
                return OWF_ERR_MEMORY;
            }
        }
        owf_free(path);
    }
    return OWF_OK;
}

/* The cells right inside the shape at index gi out to its parent, just over it in the drawing order; it goes
 * when it is a group. */
static int take_apart(owf_dpage *p, int gi, char (*made)[ODG_ORDER_ID], int max, int *count)
{
    const char *gid = p->cells[gi].id;
    char par[256], self[256];
    owf_dnum gx = p->cells[gi].geo.x, gy = p->cells[gi].geo.y;
    unsigned char *mark;
    block blk;
    int i, r, kids = 0, drop = odg_is_group(&p->cells[gi]);

    if (!p->cells[gi].parent || strlen(p->cells[gi].parent) >= sizeof par || strlen(gid) >= sizeof self)
        return OWF_ERR_FORMAT;
    snprintf(par, sizeof par, "%s", p->cells[gi].parent);
    snprintf(self, sizeof self, "%s", gid);
    if (p->cells[gi].geo.flags & OWF_DGEO_RELATIVE)
        gx = gy = 0;
    mark = owf_calloc((size_t)p->ncells + 1, 1);
    if (!mark)
        return OWF_ERR_MEMORY;
    for (i = 0; i < p->ncells; i++)
        if (i != gi && under(p, i, self))
            mark[i] = 1;                 /* what is inside, with what is inside that, moves as a block */
    for (i = 0; i < p->ncells; i++)
        if (mark[i] && same(p->cells[i].parent, self)) {
            shift(&p->cells[i], gx, gy);
            if (owf_dcell_set(&p->cells[i].parent, par) != OWF_OK) {
                owf_free(mark);
                return OWF_ERR_MEMORY;
            }
            if (made && *count < max)
                snprintf(made[*count], ODG_ORDER_ID, "%s", p->cells[i].id);
            (*count)++;
            kids++;
        }
    if (!kids) {
        owf_free(mark);
        return OWF_ERR_FORMAT;
    }
    r = take(p, mark, &blk);
    owf_free(mark);
    if (r != OWF_OK)
        return r;
    gi = owf_dpage_index(p, self);
    put(p, &blk, gi + 1);
    if (drop && (r = loosen_joined(p, self)) != OWF_OK)
        return r;
    if (drop)
        owf_dpage_remove(p, self);
    return OWF_OK;
}

int odg_ungroup(odg_doc *doc, const char *const *ids, int n, char (*made)[ODG_ORDER_ID], int max, int *nmade)
{
    owf_dpage *p = odg_page(doc);
    int i, k, count = 0, any = 0, r = OWF_OK;

    if (nmade)
        *nmade = 0;
    if (n <= 0 || !ids)
        return OWF_ERR_FORMAT;
    for (i = 0; i < n && !any; i++) {
        k = owf_dpage_index(p, ids[i]);
        if (k >= 0 && p->cells[k].kind == OWF_DCELL_VERTEX && arrangeable(p, &p->cells[k])) {
            int j;
            for (j = 0; j < p->ncells && !any; j++)
                any = same(p->cells[j].parent, p->cells[k].id);
        }
    }
    if (!any)
        return OWF_ERR_FORMAT;
    if (odg_begin(doc) != OWF_OK)
        return OWF_ERR_MEMORY;
    p = odg_page(doc);
    for (i = 0; i < n && r == OWF_OK; i++) {
        k = owf_dpage_index(p, ids[i]);
        if (k >= 0 && p->cells[k].kind == OWF_DCELL_VERTEX && arrangeable(p, &p->cells[k])) {
            r = take_apart(p, k, made, max, &count);
            if (r == OWF_ERR_FORMAT)
                r = OWF_OK;              /* nothing inside this one: the others still come apart */
        }
    }
    if (r == OWF_OK && !count)
        r = OWF_ERR_FORMAT;
    r = odg_end(doc, r);
    if (r == OWF_OK && nmade)
        *nmade = count;
    return r;
}
