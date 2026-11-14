/*
 * odg_core.c: GFOpenDiag's editing core (opendiag_core.h): operations with Undo, ports and glue,
 * orthogonal routing, snapping and hit-testing on one page of an owf_diagram. Integer arithmetic only.
 * Written for GFOpenDiag from its design (amicradle-teams designs/GFOPENDIAG-20261113.md, step D3). The
 * port style keys (exitX, exitY, entryX, entryY) and edgeStyle=orthogonalEdgeStyle are mxGraph's names
 * from its API documentation (Apache 2.0, by name only), so files keep working in diagrams.net.
 * No other program's code.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "opendiag_core.h"

#include <stdio.h>
#include <string.h>

typedef long long wide;

#define MAX_DEPTH 64

/* ---- The document ---- */

void odg_init(odg_doc *doc, owf_diagram *d, int page)
{
    const owf_dpage *p;
    const char *g;
    memset(doc, 0, sizeof *doc);
    doc->d = d;
    doc->page = page;
    p = &d->pages[page];
    g = owf_dattrs_get(&p->model, "grid");
    if (g && !strcmp(g, "1")) {
        const char *size = owf_dattrs_get(&p->model, "gridSize");
        doc->grid = size ? owf_dnum_parse(size) : OWF_DNUM(10);
        if (doc->grid < 0)
            doc->grid = 0;
    }
}

static void drop_pages(owf_dpage *pages, int n)
{
    int i;
    for (i = 0; i < n; i++)
        owf_dpage_free(&pages[i]);
}

void odg_free(odg_doc *doc)
{
    drop_pages(doc->undo, doc->nundo);
    drop_pages(doc->redo, doc->nredo);
    owf_free(doc->undo);
    owf_free(doc->redo);
    memset(doc, 0, sizeof *doc);
}

owf_dpage *odg_page(const odg_doc *doc)
{
    return &doc->d->pages[doc->page];
}

void odg_saved(odg_doc *doc)
{
    doc->changed = 0;
}

/* ---- Undo: the page as it was before each step ---- */

static int stack_push(owf_dpage **stack, int *n, owf_dpage *page)
{
    if (!*stack) {
        *stack = owf_calloc(ODG_UNDO_MAX, sizeof **stack);
        if (!*stack)
            return OWF_ERR_MEMORY;
    }
    if (*n == ODG_UNDO_MAX) {
        owf_dpage_free(&(*stack)[0]);
        memmove(*stack, *stack + 1, (ODG_UNDO_MAX - 1) * sizeof **stack);
        (*n)--;
    }
    (*stack)[(*n)++] = *page;           /* the page's arrays move to the stack */
    memset(page, 0, sizeof *page);
    return OWF_OK;
}

/* Before an operation: keeps a copy of the page for Undo. */
static int begin(odg_doc *doc)
{
    owf_dpage copy;
    memset(&copy, 0, sizeof copy);
    if (owf_dpage_copy(&copy, odg_page(doc)) != OWF_OK)
        return OWF_ERR_MEMORY;
    if (stack_push(&doc->undo, &doc->nundo, &copy) != OWF_OK) {
        owf_dpage_free(&copy);
        return OWF_ERR_MEMORY;
    }
    return OWF_OK;
}

/* After it: done, so Redo's steps go; or failed, so the page goes back as it was and the step goes. */
static int end(odg_doc *doc, int result)
{
    if (result == OWF_OK) {
        drop_pages(doc->redo, doc->nredo);
        doc->nredo = 0;
        doc->changed = 1;
    } else {
        owf_dpage *p = odg_page(doc);
        owf_dpage_free(p);
        *p = doc->undo[--doc->nundo];
        memset(&doc->undo[doc->nundo], 0, sizeof doc->undo[doc->nundo]);
    }
    return result;
}

int odg_can_undo(const odg_doc *doc)
{
    return doc->nundo > 0;
}

int odg_can_redo(const odg_doc *doc)
{
    return doc->nredo > 0;
}

static int swap_step(odg_doc *doc, owf_dpage *from, int *nfrom, owf_dpage **to, int *nto)
{
    owf_dpage *p = odg_page(doc), now = *p;
    if (!*nfrom)
        return OWF_ERR_FORMAT;
    if (stack_push(to, nto, &now) != OWF_OK)
        return OWF_ERR_MEMORY;
    *p = from[--*nfrom];
    memset(&from[*nfrom], 0, sizeof from[*nfrom]);
    doc->changed = 1;
    return OWF_OK;
}

int odg_undo(odg_doc *doc)
{
    return swap_step(doc, doc->undo, &doc->nundo, &doc->redo, &doc->nredo);
}

int odg_redo(odg_doc *doc)
{
    return swap_step(doc, doc->redo, &doc->nredo, &doc->undo, &doc->nundo);
}

/* ---- Snapping ---- */

owf_dnum odg_snap(const odg_doc *doc, owf_dnum v)
{
    owf_dnum g = doc->grid, q;
    if (g <= 0)
        return v;
    q = (v < 0 ? v - g / 2 : v + g / 2) / g;
    return q * g;
}

/* ---- Cells ---- */

static int is_layer_or_root(const owf_dpage *p, const owf_dcell *c)
{
    const owf_dcell *parent = owf_dpage_find(p, c->parent);
    return !c->parent || !parent || !parent->parent;
}

static owf_dcell *shape(const owf_dpage *p, const char *id)
{
    owf_dcell *c = owf_dpage_find(p, id);
    return c && c->kind == OWF_DCELL_VERTEX ? c : NULL;
}

static int is_layer(const owf_dpage *p, const owf_dcell *c)
{
    const owf_dcell *parent = c->parent ? owf_dpage_find(p, c->parent) : NULL;
    return parent && !parent->parent;
}

/* Layer i's index in the page's cells, -1 when there is none */
static int layer_index(const owf_dpage *p, int i)
{
    int k, n = 0;
    for (k = 0; k < p->ncells; k++)
        if (is_layer(p, &p->cells[k]) && n++ == i)
            return k;
    return -1;
}

int odg_layer_count(const odg_doc *doc)
{
    const owf_dpage *p = odg_page(doc);
    int k, n = 0;
    for (k = 0; k < p->ncells; k++)
        n += is_layer(p, &p->cells[k]);
    return n;
}

const owf_dcell *odg_layer_at(const odg_doc *doc, int i)
{
    const owf_dpage *p = odg_page(doc);
    int k = i >= 0 ? layer_index(p, i) : -1;
    return k >= 0 ? &p->cells[k] : NULL;
}

int odg_layer_shown(const odg_doc *doc, int i)
{
    const owf_dcell *c = odg_layer_at(doc, i);
    const char *v = c ? owf_dattrs_get(&c->extra, "visible") : NULL;
    return c && !(v && !strcmp(v, "0"));
}

static int style_locked(const char *style)
{
    char v[8];
    return owf_dstyle_get(style, "locked", v, sizeof v) && !strcmp(v, "1");
}

int odg_layer_locked(const odg_doc *doc, int i)
{
    const owf_dcell *c = odg_layer_at(doc, i);
    return c && style_locked(c->style);
}

int odg_cell_locked(const odg_doc *doc, const owf_dcell *c)
{
    const owf_dpage *p = odg_page(doc);
    int depth = 0;
    while (c && depth++ < 64) {
        if (style_locked(c->style))
            return 1;
        c = c->parent ? owf_dpage_find(p, c->parent) : NULL;
    }
    return 0;
}

int odg_layer_current(const odg_doc *doc)
{
    if (odg_layer_at(doc, doc->layer))
        return doc->layer;
    return odg_layer_at(doc, 0) ? 0 : -1;
}

/* The layer new cells go on (odg_layer_current), NULL when there is none or it is hidden or locked. */
static const char *layer_for(const odg_doc *doc)
{
    int i = odg_layer_current(doc);
    if (i < 0 || !odg_layer_shown(doc, i) || odg_layer_locked(doc, i))
        return NULL;
    return odg_layer_at(doc, i)->id;
}

static int route_at(odg_doc *doc, int index);

static void put_id(char *out, const char *id)
{
    if (out)
        snprintf(out, 24, "%s", id);
}

int odg_add_shape(odg_doc *doc, owf_dnum x, owf_dnum y, owf_dnum w, owf_dnum h, const char *style,
                  const char *value, char *id)
{
    owf_dpage *p = odg_page(doc);
    const char *lay = layer_for(doc);
    owf_dcell *c;
    int r;
    if (!lay || w <= 0 || h <= 0)
        return OWF_ERR_FORMAT;
    if (begin(doc) != OWF_OK)
        return OWF_ERR_MEMORY;
    p = odg_page(doc);
    c = owf_dpage_add(p, NULL, layer_for(doc), OWF_DCELL_VERTEX);
    r = c ? OWF_OK : OWF_ERR_MEMORY;
    if (c) {
        c->geo.flags = OWF_DGEO_HAS;
        c->geo.x = odg_snap(doc, x);
        c->geo.y = odg_snap(doc, y);
        c->geo.width = w;
        c->geo.height = h;
        if (owf_dcell_set(&c->style, style) != OWF_OK || owf_dcell_set(&c->value, value) != OWF_OK)
            r = OWF_ERR_MEMORY;
        else
            put_id(id, c->id);
    }
    return end(doc, r);
}

static int is_orthogonal(const owf_dcell *e)
{
    char v[32];
    return owf_dstyle_get(e->style, "edgeStyle", v, sizeof v) && !strcmp(v, "orthogonalEdgeStyle");
}

int odg_add_edge(odg_doc *doc, const char *source, const char *target, const owf_dpoint *loose_source,
                 const owf_dpoint *loose_target, const char *style, char *id)
{
    owf_dpage *p = odg_page(doc);
    owf_dcell *e;
    int r = OWF_OK, at;
    if ((source && !shape(p, source)) || (target && !shape(p, target)) || (!source && !loose_source) ||
        (!target && !loose_target) || !layer_for(doc))
        return OWF_ERR_FORMAT;
    if (begin(doc) != OWF_OK)
        return OWF_ERR_MEMORY;
    p = odg_page(doc);
    e = owf_dpage_add(p, NULL, layer_for(doc), OWF_DCELL_EDGE);
    if (!e)
        return end(doc, OWF_ERR_MEMORY);
    e->geo.flags = OWF_DGEO_HAS | OWF_DGEO_RELATIVE;
    if (owf_dcell_set(&e->source, source) || owf_dcell_set(&e->target, target) || owf_dcell_set(&e->style, style))
        return end(doc, OWF_ERR_MEMORY);
    if (!source) {
        e->geo.flags |= OWF_DGEO_SOURCE_POINT;
        e->geo.source_point = *loose_source;
    }
    if (!target) {
        e->geo.flags |= OWF_DGEO_TARGET_POINT;
        e->geo.target_point = *loose_target;
    }
    at = p->ncells - 1;                  /* the new connector is the last cell */
    if (is_orthogonal(e))
        r = route_at(doc, at);
    if (r == OWF_OK)
        put_id(id, odg_page(doc)->cells[at].id);
    return end(doc, r);
}

/* ---- Ports ---- */

owf_dpoint odg_port_point(const owf_dnum box[4], odg_port port)
{
    owf_dpoint at;
    at.x = box[0] + box[2] / 2;
    at.y = box[1] + box[3] / 2;
    switch (port) {
    case ODG_PORT_N: at.y = box[1]; break;
    case ODG_PORT_E: at.x = box[0] + box[2]; break;
    case ODG_PORT_S: at.y = box[1] + box[3]; break;
    case ODG_PORT_W: at.x = box[0]; break;
    default: break;
    }
    return at;
}

static const char *const port_x[4] = { "0.5", "1", "0.5", "0" };
static const char *const port_y[4] = { "0", "0.5", "1", "0.5" };

int odg_connect(odg_doc *doc, const char *edge, int end_no, const char *to, odg_port port,
                const owf_dpoint *loose)
{
    owf_dpage *p = odg_page(doc);
    int at = owf_dpage_index(p, edge);
    owf_dcell *e = at >= 0 ? &p->cells[at] : NULL;
    const char *kx = end_no ? "entryX" : "exitX", *ky = end_no ? "entryY" : "exitY";
    int r = OWF_OK;
    if (!e || e->kind != OWF_DCELL_EDGE || (to && !shape(p, to)) || (!to && !loose) || port < ODG_PORT_NONE ||
        port > ODG_PORT_W)
        return OWF_ERR_FORMAT;
    if (begin(doc) != OWF_OK)
        return OWF_ERR_MEMORY;
    p = odg_page(doc);
    if (at >= p->ncells)
        return end(doc, OWF_ERR_FORMAT);
    e = &p->cells[at];                   /* begin() copied the page aside; this one's cells stay where they were */
    if (owf_dcell_set(end_no ? &e->target : &e->source, to) != OWF_OK)
        return end(doc, OWF_ERR_MEMORY);
    if (to) {
        e->geo.flags &= ~(end_no ? OWF_DGEO_TARGET_POINT : OWF_DGEO_SOURCE_POINT);
    } else {
        e->geo.flags |= end_no ? OWF_DGEO_TARGET_POINT : OWF_DGEO_SOURCE_POINT;
        if (end_no)
            e->geo.target_point = *loose;
        else
            e->geo.source_point = *loose;
    }
    if (owf_dstyle_set(&e->style, kx, to && port != ODG_PORT_NONE ? port_x[port] : NULL) != OWF_OK ||
        owf_dstyle_set(&e->style, ky, to && port != ODG_PORT_NONE ? port_y[port] : NULL) != OWF_OK)
        return end(doc, OWF_ERR_MEMORY);
    if (is_orthogonal(e))
        r = route_at(doc, at);
    return end(doc, r);
}

/* ---- Routing ---- */

/* Which side a port's fractions are on; ODG_PORT_NONE when they are inside or not there. */
static odg_port side_of(const char *style, const char *kx, const char *ky)
{
    char bx[24], by[24];
    owf_dnum fx, fy;
    if (!owf_dstyle_get(style, kx, bx, sizeof bx) || !owf_dstyle_get(style, ky, by, sizeof by))
        return ODG_PORT_NONE;
    fx = owf_dnum_parse(bx);
    fy = owf_dnum_parse(by);
    if (fy <= 0) return ODG_PORT_N;
    if (fy >= 100) return ODG_PORT_S;
    if (fx <= 0) return ODG_PORT_W;
    if (fx >= 100) return ODG_PORT_E;
    return ODG_PORT_NONE;
}

static int horizontal(odg_port s)
{
    return s == ODG_PORT_E || s == ODG_PORT_W;
}

int odg_route(odg_doc *doc, const char *id)
{
    return route_at(doc, owf_dpage_index(odg_page(doc), id));
}

/* Routes the connector at index (cells are held by index across an operation: an id is never copied into a
 * buffer that could cut it). */
static int route_at(odg_doc *doc, int index)
{
    owf_dpage *p = odg_page(doc);
    owf_dcell *e = index >= 0 && index < p->ncells ? &p->cells[index] : NULL;
    owf_dcell *src, *dst;
    owf_dnum sb[4], tb[4];
    owf_dpoint s, t;
    odg_port ss, ts;
    int has_s, has_t;
    wide dx, dy;

    if (!e || e->kind != OWF_DCELL_EDGE)
        return OWF_ERR_FORMAT;
    src = owf_dpage_find(p, e->source);
    dst = owf_dpage_find(p, e->target);
    has_s = src && owf_dcell_box(p, src, sb);
    has_t = dst && owf_dcell_box(p, dst, tb);
    if ((!has_s && !(e->geo.flags & OWF_DGEO_SOURCE_POINT)) || (!has_t && !(e->geo.flags & OWF_DGEO_TARGET_POINT)))
        return OWF_ERR_FORMAT;
    s = has_s ? odg_port_point(sb, ODG_PORT_NONE) : e->geo.source_point;
    t = has_t ? odg_port_point(tb, ODG_PORT_NONE) : e->geo.target_point;
    dx = t.x - s.x;
    dy = t.y - s.y;
    /* the sides: the ports' when they name one, else the sides facing the other end */
    ss = has_s ? side_of(e->style, "exitX", "exitY") : ODG_PORT_NONE;
    ts = has_t ? side_of(e->style, "entryX", "entryY") : ODG_PORT_NONE;
    if (ss == ODG_PORT_NONE)
        ss = (dx < 0 ? -dx : dx) >= (dy < 0 ? -dy : dy) ? (dx >= 0 ? ODG_PORT_E : ODG_PORT_W)
                                                        : (dy >= 0 ? ODG_PORT_S : ODG_PORT_N);
    if (ts == ODG_PORT_NONE)
        ts = horizontal(ss) ? (dx >= 0 ? ODG_PORT_W : ODG_PORT_E) : (dy >= 0 ? ODG_PORT_N : ODG_PORT_S);
    if (has_s)
        s = odg_port_point(sb, ss);
    if (has_t)
        t = odg_port_point(tb, ts);
    e->geo.npoints = 0;
    e->geo.flags |= OWF_DGEO_POINTS;
    if (horizontal(ss) && horizontal(ts)) {
        if (s.y != t.y) {
            owf_dnum mx = s.x + (t.x - s.x) / 2;
            if (owf_dgeo_add_point(&e->geo, mx, s.y) || owf_dgeo_add_point(&e->geo, mx, t.y))
                return OWF_ERR_MEMORY;
        }
    } else if (!horizontal(ss) && !horizontal(ts)) {
        if (s.x != t.x) {
            owf_dnum my = s.y + (t.y - s.y) / 2;
            if (owf_dgeo_add_point(&e->geo, s.x, my) || owf_dgeo_add_point(&e->geo, t.x, my))
                return OWF_ERR_MEMORY;
        }
    } else if (horizontal(ss)) {
        if (owf_dgeo_add_point(&e->geo, t.x, s.y))
            return OWF_ERR_MEMORY;
    } else if (owf_dgeo_add_point(&e->geo, s.x, t.y))
        return OWF_ERR_MEMORY;
    return OWF_OK;
}

/* ---- Moving, resizing, deleting ---- */

/* 1 when the cell, or a cell it is inside, is marked. */
static int inside_marked(const owf_dpage *p, const unsigned char *mark, const char *id)
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

/* Routes again every orthogonal connector with an end on or inside a marked shape (and not both ends
 * when both_too is 0). */
static int reroute_joined(odg_doc *doc, const unsigned char *mark, int both_too)
{
    owf_dpage *p = odg_page(doc);
    int i, r;
    for (i = 0; i < p->ncells; i++) {
        const owf_dcell *e = &p->cells[i];
        int s, t;
        if (e->kind != OWF_DCELL_EDGE || !is_orthogonal(e) || mark[i])
            continue;
        s = inside_marked(p, mark, e->source);
        t = inside_marked(p, mark, e->target);
        if (!(s || t) || (s && t && !both_too))
            continue;
        if ((r = route_at(doc, i)) != OWF_OK && r != OWF_ERR_FORMAT)
            return r;
        p = odg_page(doc);
    }
    return OWF_OK;
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

int odg_move(odg_doc *doc, const char *const *ids, int n, owf_dnum dx, owf_dnum dy)
{
    owf_dpage *p = odg_page(doc);
    unsigned char *mark;
    int i, any = 0, first = -1, r;

    if (n <= 0)
        return OWF_ERR_FORMAT;
    for (i = 0; i < n; i++) {
        owf_dcell *c = owf_dpage_find(p, ids[i]);
        if (c && (c->kind == OWF_DCELL_VERTEX || c->kind == OWF_DCELL_EDGE) && !is_layer_or_root(p, c)) {
            any = 1;
            if (first < 0 && c->kind == OWF_DCELL_VERTEX)
                first = owf_dpage_index(p, ids[i]);
        }
    }
    if (!any)
        return OWF_ERR_FORMAT;
    if (first >= 0) {                    /* the first shape's corner lands on the grid */
        const owf_dcell *c = &p->cells[first];
        dx = odg_snap(doc, c->geo.x + dx) - c->geo.x;
        dy = odg_snap(doc, c->geo.y + dy) - c->geo.y;
    }
    if (begin(doc) != OWF_OK)
        return OWF_ERR_MEMORY;
    p = odg_page(doc);
    mark = owf_calloc((size_t)p->ncells + 1, 1);
    if (!mark)
        return end(doc, OWF_ERR_MEMORY);
    for (i = 0; i < n; i++) {
        int k = owf_dpage_index(p, ids[i]);
        if (k >= 0 && !is_layer_or_root(p, &p->cells[k]))
            mark[k] = 1;
    }
    for (i = 0; i < p->ncells; i++) {
        owf_dcell *c = &p->cells[i];
        if (c->kind == OWF_DCELL_VERTEX && mark[i] && !inside_marked(p, mark, c->parent)) {
            c->geo.x += dx;              /* what is inside it moves with it */
            c->geo.y += dy;
        } else if (c->kind == OWF_DCELL_EDGE) {
            /* chosen, or both its ends joined to shapes that move: its bends go along. A loose end
             * stays where it is unless the connector itself is chosen. */
            if (mark[i] || (c->source && c->target && inside_marked(p, mark, c->source) &&
                            inside_marked(p, mark, c->target)))
                shift_edge(c, dx, dy);
        }
    }
    r = reroute_joined(doc, mark, 0);
    owf_free(mark);
    return end(doc, r);
}

int odg_resize(odg_doc *doc, const char *id, owf_dnum x, owf_dnum y, owf_dnum w, owf_dnum h)
{
    owf_dpage *p = odg_page(doc);
    owf_dcell *c = shape(p, id);
    unsigned char *mark;
    int r;
    if (!c || w <= 0 || h <= 0 || is_layer_or_root(p, c))
        return OWF_ERR_FORMAT;
    if (begin(doc) != OWF_OK)
        return OWF_ERR_MEMORY;
    p = odg_page(doc);
    c = shape(p, id);
    c->geo.x = odg_snap(doc, x);
    c->geo.y = odg_snap(doc, y);
    c->geo.width = w;
    c->geo.height = h;
    mark = owf_calloc((size_t)p->ncells + 1, 1);
    if (!mark)
        return end(doc, OWF_ERR_MEMORY);
    mark[owf_dpage_index(p, id)] = 1;
    r = reroute_joined(doc, mark, 1);
    owf_free(mark);
    return end(doc, r);
}

int odg_delete(odg_doc *doc, const char *const *ids, int n)
{
    owf_dpage *p = odg_page(doc);
    int i, gone = 0;
    for (i = 0; i < n; i++) {
        const owf_dcell *c = owf_dpage_find(p, ids[i]);
        if (c && is_layer_or_root(p, c))
            return OWF_ERR_FORMAT;
    }
    for (i = 0; i < n && !gone; i++)
        gone = owf_dpage_find(p, ids[i]) != NULL;
    if (!gone)
        return OWF_ERR_FORMAT;
    if (begin(doc) != OWF_OK)
        return OWF_ERR_MEMORY;
    p = odg_page(doc);
    for (i = 0; i < n; i++)
        owf_dpage_remove(p, ids[i]);
    return end(doc, OWF_OK);
}

int odg_set_value(odg_doc *doc, const char *id, const char *value)
{
    owf_dcell *c = owf_dpage_find(odg_page(doc), id);
    if (!c)
        return OWF_ERR_FORMAT;
    if (begin(doc) != OWF_OK)
        return OWF_ERR_MEMORY;
    c = owf_dpage_find(odg_page(doc), id);
    return end(doc, owf_dcell_set(&c->value, value));
}

int odg_set_style(odg_doc *doc, const char *const *ids, int n, const char *key, const char *value)
{
    return odg_set_styles(doc, ids, n, &key, &value, 1);
}

int odg_set_styles(odg_doc *doc, const char *const *ids, int n, const char *const *keys, const char *const *values,
                   int nkeys)
{
    owf_dpage *p = odg_page(doc);
    int i, k, any = 0, r = OWF_OK;
    for (i = 0; i < n; i++)
        any |= owf_dpage_find(p, ids[i]) != NULL;
    if (!any || nkeys < 1)
        return OWF_ERR_FORMAT;
    if (begin(doc) != OWF_OK)
        return OWF_ERR_MEMORY;
    p = odg_page(doc);
    for (i = 0; i < n && r == OWF_OK; i++) {
        owf_dcell *c = owf_dpage_find(p, ids[i]);
        for (k = 0; c && k < nkeys && r == OWF_OK; k++)
            r = owf_dstyle_set(&c->style, keys[k], values[k]);
    }
    return end(doc, r);
}

/* ---- Layers ---- */

int odg_layer_add(odg_doc *doc, const char *name, int *at)
{
    owf_dpage *p = odg_page(doc);
    const owf_dcell *root = NULL;
    owf_dcell *c;
    char made[32];
    int k, n = odg_layer_count(doc), r;
    for (k = 0; k < p->ncells && !root; k++)
        if (!p->cells[k].parent)
            root = &p->cells[k];
    if (!root)
        return OWF_ERR_FORMAT;
    if (!name) {
        snprintf(made, sizeof made, "Layer %d", n + 1);
        name = made;
    }
    if (begin(doc) != OWF_OK)
        return OWF_ERR_MEMORY;
    p = odg_page(doc);
    for (k = 0; k < p->ncells; k++)
        if (!p->cells[k].parent)
            break;
    {
        /* the root's id copied whole (adding a cell may move the cells), however long another tool made it */
        size_t len = strlen(p->cells[k].id) + 1;
        char *rootid = (char *)owf_malloc(len);
        if (!rootid)
            return end(doc, OWF_ERR_MEMORY);
        memcpy(rootid, p->cells[k].id, len);
        c = owf_dpage_add(p, NULL, rootid, OWF_DCELL_PLAIN);
        owf_free(rootid);
    }
    if (!c)
        return end(doc, OWF_ERR_MEMORY);
    r = owf_dcell_set(&c->value, name);
    if (r == OWF_OK) {
        doc->layer = n;
        if (at)
            *at = n;
    }
    return end(doc, r);
}

int odg_layer_show(odg_doc *doc, int i, int shown)
{
    int k = i >= 0 ? layer_index(odg_page(doc), i) : -1;
    if (k < 0)
        return OWF_ERR_FORMAT;
    if (begin(doc) != OWF_OK)
        return OWF_ERR_MEMORY;
    return end(doc, owf_dattrs_set(&odg_page(doc)->cells[k].extra, "visible", shown ? NULL : "0"));
}

int odg_layer_lock(odg_doc *doc, int i, int locked)
{
    int k = i >= 0 ? layer_index(odg_page(doc), i) : -1;
    if (k < 0)
        return OWF_ERR_FORMAT;
    if (begin(doc) != OWF_OK)
        return OWF_ERR_MEMORY;
    return end(doc, owf_dstyle_set(&odg_page(doc)->cells[k].style, "locked", locked ? "1" : NULL));
}

int odg_layer_rename(odg_doc *doc, int i, const char *name)
{
    int k = i >= 0 ? layer_index(odg_page(doc), i) : -1;
    if (k < 0 || !name)
        return OWF_ERR_FORMAT;
    if (begin(doc) != OWF_OK)
        return OWF_ERR_MEMORY;
    return end(doc, owf_dcell_set(&odg_page(doc)->cells[k].value, name));
}

/* ---- Hit-testing ---- */

/* A whole square root, for the hit test's distances. */
static wide isqrt_wide(wide v)
{
    wide r = 0, bit = (wide)1 << 62;
    if (v <= 0)
        return 0;
    while (bit > v)
        bit >>= 2;
    while (bit) {
        if (v >= r + bit) {
            v -= r + bit;
            r = (r >> 1) + bit;
        } else
            r >>= 1;
        bit >>= 2;
    }
    return r;
}

/* 1 when (x, y) is within tol of the segment a-b. No product is more than two lengths multiplied: the distance
 * from the line is the cross product over the length, and lengths past 2^30 hundredths are halved first (with tol),
 * so a connector of any length can't overflow. */
static int near_segment(owf_dpoint a, owf_dpoint b, owf_dnum x, owf_dnum y, owf_dnum tol)
{
    wide vx = (wide)b.x - a.x, vy = (wide)b.y - a.y, wx = (wide)x - a.x, wy = (wide)y - a.y, t = tol, lim = (wide)1 << 30;
    wide len2, dot, cross, ux, uy;
    while (vx > lim || -vx > lim || vy > lim || -vy > lim || wx > lim || -wx > lim || wy > lim || -wy > lim) {
        vx /= 2;
        vy /= 2;
        wx /= 2;
        wy /= 2;
        t /= 2;
    }
    len2 = vx * vx + vy * vy;
    dot = vx * wx + vy * wy;
    if (len2 == 0 || dot <= 0)
        return wx * wx + wy * wy <= t * t;
    if (dot >= len2) {
        ux = wx - vx;
        uy = wy - vy;
        return ux * ux + uy * uy <= t * t;
    }
    cross = vx * wy - vy * wx;
    if (cross < 0)
        cross = -cross;
    return cross <= t * (isqrt_wide(len2) + 1);
}

static int hit_from(const odg_doc *doc, owf_dnum x, owf_dnum y, owf_dnum tol, char *id, size_t size, int open)
{
    const owf_dpage *p = odg_page(doc);
    int i, k, hit;
    for (i = p->ncells - 1; i >= 0; i--) {
        const owf_dcell *c = &p->cells[i];
        owf_dnum box[4];
        hit = 0;
        if (!owf_dcell_shown(p, c))      /* hidden, or on a hidden layer */
            continue;
        if (open && (c->kind == OWF_DCELL_VERTEX || c->kind == OWF_DCELL_EDGE) && odg_cell_locked(doc, c))
            continue;
        if (c->kind == OWF_DCELL_VERTEX && owf_dcell_box(p, c, box))
            hit = x >= box[0] && x <= box[0] + box[2] && y >= box[1] && y <= box[1] + box[3];
        else if (c->kind == OWF_DCELL_EDGE) {
            int n;
            owf_dpoint *pts = owf_dedge_path(p, c, &n);
            for (k = 1; k < n && !hit; k++)
                hit = near_segment(pts[k - 1], pts[k], x, y, tol);
            owf_free(pts);
        }
        if (hit) {
            if (size)
                snprintf(id, size, "%s", c->id);
            return 1;
        }
    }
    return 0;
}

int odg_hit(const odg_doc *doc, owf_dnum x, owf_dnum y, owf_dnum tol, char *id, size_t size)
{
    return hit_from(doc, x, y, tol, id, size, 0);
}

int odg_hit_open(const odg_doc *doc, owf_dnum x, owf_dnum y, owf_dnum tol, char *id, size_t size)
{
    return hit_from(doc, x, y, tol, id, size, 1);
}
