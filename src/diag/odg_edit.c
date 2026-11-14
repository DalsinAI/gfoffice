/*
 * odg_edit.c: GFOpenDiag's mouse on the canvas (opendiag_edit.h): select, lasso, move, resize by a handle, draw a
 * shape and join shapes with a connector, each done through the editing core as one Undo step.
 * Cleanroom: written from the design (amicradle-teams designs/GFOPENDIAG-20261113.md) and this repository's own
 * core (opendiag_core.h) and canvas (opendiag_view.h); no other program's code was read for it.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <string.h>
#include <stdio.h>
#include "opendiag_edit.h"

static const char *const tool_names[ODG_NTOOLS] = {
    "Select", "Box", "Rounded", "Ellipse", "Rhombus", "Text", "Connector", "Shape"
};
static const char *const tool_styles[ODG_NTOOLS] = {
    "",
    "rounded=0;whiteSpace=wrap;html=1;",
    "rounded=1;whiteSpace=wrap;html=1;",
    "ellipse;whiteSpace=wrap;html=1;",
    "rhombus;whiteSpace=wrap;html=1;",
    "text;html=1;align=center;verticalAlign=middle;whiteSpace=wrap;",
    "edgeStyle=orthogonalEdgeStyle;rounded=0;html=1;",
    ""                                   /* the palette's: odg_stencil_at(e->shape)->style */
};

const char *odg_tool_name(odg_tool t)
{
    return t >= 0 && t < ODG_NTOOLS ? tool_names[t] : "";
}

const char *odg_tool_style(odg_tool t)
{
    return t >= 0 && t < ODG_NTOOLS ? tool_styles[t] : "";
}

static void relink(odg_edit *e)
{
    int i;
    for (i = 0; i < e->n; i++)
        e->list[i] = e->ids[i];
}

void odg_edit_init(odg_edit *e)
{
    memset(e, 0, sizeof *e);
    e->tool = ODG_TOOL_SELECT;
    e->handle = ODG_HANDLE_NONE;
}

void odg_edit_show(odg_edit *e, odg_view *v)
{
    relink(e);
    v->selected = e->n ? e->list : NULL;
    v->nselected = e->n;
}

int odg_edit_has(const odg_edit *e, const char *id)
{
    int i;
    for (i = 0; i < e->n; i++)
        if (!strcmp(e->ids[i], id))
            return 1;
    return 0;
}

void odg_edit_clear(odg_edit *e)
{
    e->n = 0;
}

int odg_edit_select(odg_edit *e, const char *id, int add)
{
    size_t len = strlen(id);
    if (len >= ODG_EDIT_ID)
        return 0;
    if (!add)
        e->n = 0;
    if (odg_edit_has(e, id))
        return 1;
    if (e->n >= ODG_EDIT_MAX)
        return 0;
    memcpy(e->ids[e->n], id, len + 1);
    e->n++;
    relink(e);
    return 1;
}

void odg_edit_drop(odg_edit *e, const char *id)
{
    int i;
    for (i = 0; i < e->n; i++)
        if (!strcmp(e->ids[i], id)) {
            memmove(e->ids[i], e->ids[i + 1], (size_t)(e->n - i - 1) * ODG_EDIT_ID);
            e->n--;
            break;
        }
    relink(e);
}

/* A cell the mouse can pick: a shown shape (not a connector's label) or a shown connector, not locked. */
static int pickable(const odg_doc *doc, const owf_dcell *c, int shapes)
{
    const owf_dpage *p = odg_page(doc);
    const owf_dcell *parent;
    if (odg_cell_locked(doc, c))
        return 0;
    if (c->kind == OWF_DCELL_EDGE)
        return !shapes && owf_dcell_shown(p, c);
    if (c->kind != OWF_DCELL_VERTEX || !owf_dcell_shown(p, c))
        return 0;
    parent = c->parent ? owf_dpage_find(p, c->parent) : NULL;
    return !shapes || !parent || parent->kind != OWF_DCELL_EDGE;
}

int odg_edit_pickable(const odg_doc *doc, const char *id)
{
    const owf_dcell *c = id ? owf_dpage_find(odg_page(doc), id) : NULL;
    return c && pickable(doc, c, 0);
}

void odg_edit_prune(odg_edit *e, const odg_doc *doc)
{
    const owf_dpage *p = odg_page(doc);
    int i = 0;
    while (i < e->n) {
        const owf_dcell *c = owf_dpage_find(p, e->ids[i]);
        /* gone, or hidden or locked since it was picked: Delete, a nudge and Style leave it alone */
        if (!c || !pickable(doc, c, 0))
            odg_edit_drop(e, e->ids[i]);
        else
            i++;
    }
    relink(e);
}

int odg_edit_select_all(odg_edit *e, const odg_doc *doc)
{
    const owf_dpage *p = odg_page(doc);
    int i;
    e->n = 0;
    for (i = 0; i < p->ncells && e->n < ODG_EDIT_MAX; i++)
        if (pickable(doc, &p->cells[i], 0))
            odg_edit_select(e, p->cells[i].id, 1);
    return e->n;
}

static owf_dnum tolerance(const odg_view *v)
{
    int zoom = v->zoom < ODG_ZOOM_MIN ? ODG_ZOOM_MIN : v->zoom;
    return (owf_dnum)((long)ODG_EDIT_TOLERANCE * 10000L / zoom);
}

int odg_edit_hit(const odg_view *v, const odg_doc *doc, long px, long py, int shapes, char *id)
{
    const owf_dpage *p = odg_page(doc);
    owf_dnum x = odg_page_x(v, px), y = odg_page_y(v, py);
    char buf[ODG_EDIT_ID + 1];
    int i;
    if (!shapes) {
        const owf_dcell *c;
        if (!odg_hit_open(doc, x, y, tolerance(v), buf, sizeof buf) || strlen(buf) >= ODG_EDIT_ID)
            return 0;
        c = owf_dpage_find(p, buf);
        if (!c || (c->kind != OWF_DCELL_VERTEX && c->kind != OWF_DCELL_EDGE) || odg_cell_locked(doc, c))
            return 0;
        memcpy(id, buf, strlen(buf) + 1);
        return 1;
    }
    for (i = p->ncells - 1; i >= 0; i--) {
        const owf_dcell *c = &p->cells[i];
        owf_dnum box[4];
        if (!pickable(doc, c, 1) || !owf_dcell_box(p, c, box))
            continue;
        if (x >= box[0] && x <= box[0] + box[2] && y >= box[1] && y <= box[1] + box[3]) {
            if (strlen(c->id) >= ODG_EDIT_ID)
                return 0;
            memcpy(id, c->id, strlen(c->id) + 1);
            return 1;
        }
    }
    return 0;
}

odg_drag odg_edit_press(odg_edit *e, const odg_view *v, const odg_doc *doc, long px, long py, int shift)
{
    char id[ODG_EDIT_ID];
    e->x0 = e->x1 = px;
    e->y0 = e->y1 = py;
    e->moved = 0;
    e->add = shift;
    e->from[0] = 0;
    e->made[0] = 0;
    e->handle = ODG_HANDLE_NONE;
    e->drag = ODG_DRAG_NONE;
    switch (e->tool) {
    case ODG_TOOL_SELECT: {
        odg_view shown = *v;
        char hid[ODG_EDIT_ID + 1];
        odg_handle h;
        odg_edit_show(e, &shown);
        h = odg_view_handle(&shown, doc, px, py, hid, sizeof hid);
        if (h != ODG_HANDLE_NONE && strlen(hid) < ODG_EDIT_ID) {
            e->handle = h;
            memcpy(e->from, hid, strlen(hid) + 1);
            e->drag = ODG_DRAG_RESIZE;
        } else if (odg_edit_hit(v, doc, px, py, 0, id)) {
            if (shift && odg_edit_has(e, id)) {
                odg_edit_drop(e, id);
            } else {
                if (!odg_edit_has(e, id))
                    odg_edit_select(e, id, shift);
                memcpy(e->from, id, strlen(id) + 1);
                e->drag = ODG_DRAG_MOVE;
            }
        } else {
            if (!shift)
                odg_edit_clear(e);
            e->drag = ODG_DRAG_LASSO;
        }
        break;
    }
    case ODG_TOOL_CONNECTOR:
        if (odg_edit_hit(v, doc, px, py, 1, id))
            memcpy(e->from, id, strlen(id) + 1);
        e->drag = ODG_DRAG_CONNECT;
        break;
    default:
        e->drag = ODG_DRAG_CREATE;
        break;
    }
    return e->drag;
}

void odg_edit_motion(odg_edit *e, long px, long py)
{
    long dx, dy;
    if (e->drag == ODG_DRAG_NONE)
        return;
    e->x1 = px;
    e->y1 = py;
    dx = px - e->x0;
    dy = py - e->y0;
    if (dx >= ODG_EDIT_SLOP || dx <= -ODG_EDIT_SLOP || dy >= ODG_EDIT_SLOP || dy <= -ODG_EDIT_SLOP)
        e->moved = 1;
}

void odg_edit_cancel(odg_edit *e)
{
    e->drag = ODG_DRAG_NONE;
    e->moved = 0;
}

/* A cell's box on the page: a shape's own, a connector's path's; 0 when it has none. */
static int cell_bounds(const owf_dpage *p, const owf_dcell *c, owf_dnum box[4])
{
    int n, k;
    owf_dpoint *pts;
    owf_dnum l, t, r, b;
    if (c->kind == OWF_DCELL_VERTEX)
        return owf_dcell_box(p, c, box);
    if (c->kind != OWF_DCELL_EDGE || !(pts = owf_dedge_path(p, c, &n)))
        return 0;
    if (n < 1) {
        owf_free(pts);
        return 0;
    }
    l = r = pts[0].x;
    t = b = pts[0].y;
    for (k = 1; k < n; k++) {
        if (pts[k].x < l) l = pts[k].x;
        if (pts[k].x > r) r = pts[k].x;
        if (pts[k].y < t) t = pts[k].y;
        if (pts[k].y > b) b = pts[k].y;
    }
    owf_free(pts);
    box[0] = l;
    box[1] = t;
    box[2] = r - l;
    box[3] = b - t;
    return 1;
}

/* The box a resize gives, on the page; 0 when the shape is gone. */
static int resized(const odg_edit *e, const odg_view *v, const odg_doc *doc, owf_dnum out[4])
{
    const owf_dpage *p = odg_page(doc);
    const owf_dcell *c = owf_dpage_find(p, e->from);
    owf_dnum box[4];
    if (!c || c->kind != OWF_DCELL_VERTEX || !owf_dcell_box(p, c, box))
        return 0;
    odg_handle_drag(v, doc, box, e->handle, e->x1 - e->x0, e->y1 - e->y0, out);
    return 1;
}

static void corners(oldraw_rect *r, long x0, long y0, long x1, long y1)
{
    r->x = (int)x0;
    r->y = (int)y0;
    r->w = (int)(x1 - x0);
    r->h = (int)(y1 - y0);
}

int odg_edit_feedback(const odg_edit *e, const odg_view *v, const odg_doc *doc, oldraw_rect *r)
{
    const owf_dpage *p = odg_page(doc);
    owf_dnum box[4];
    oldraw_rect s;
    int i, any = 0;
    long l = 0, t = 0, rr = 0, b = 0;
    if (e->drag == ODG_DRAG_NONE || !e->moved)
        return 0;
    switch (e->drag) {
    case ODG_DRAG_MOVE:
        for (i = 0; i < e->n; i++) {
            const owf_dcell *c = owf_dpage_find(p, e->ids[i]);
            if (!c || !cell_bounds(p, c, box))
                continue;
            odg_view_rect(v, box, &s);
            if (!any || s.x < l) l = s.x;
            if (!any || s.y < t) t = s.y;
            if (!any || s.x + s.w > rr) rr = s.x + s.w;
            if (!any || s.y + s.h > b) b = s.y + s.h;
            any = 1;
        }
        if (!any)
            return 0;
        corners(r, l + e->x1 - e->x0, t + e->y1 - e->y0, rr - 1 + e->x1 - e->x0, b - 1 + e->y1 - e->y0);
        return 1;
    case ODG_DRAG_RESIZE:
        if (!resized(e, v, doc, box))
            return 0;
        odg_view_rect(v, box, &s);
        corners(r, s.x, s.y, s.x + s.w - 1, s.y + s.h - 1);
        return 1;
    default:
        corners(r, e->x0, e->y0, e->x1, e->y1);
        return 1;
    }
}

static int is_shape(const owf_dpage *p, const char *id)
{
    const owf_dcell *c = id && *id ? owf_dpage_find(p, id) : NULL;
    return c && c->kind == OWF_DCELL_VERTEX;
}

static int do_move(odg_edit *e, const odg_view *v, odg_doc *doc)
{
    const char *list[ODG_EDIT_MAX];
    int i, k = 0;
    owf_dnum dx = odg_page_x(v, e->x1) - odg_page_x(v, e->x0), dy = odg_page_y(v, e->y1) - odg_page_y(v, e->y0);
    if (!e->n || (!dx && !dy))
        return OWF_OK;
    /* the shape pressed on first: the core snaps the first one's corner, so it lands on the grid */
    if (e->from[0] && odg_edit_has(e, e->from))
        list[k++] = e->from;
    for (i = 0; i < e->n; i++)
        if (strcmp(e->ids[i], e->from) || !k)
            list[k++] = e->ids[i];
    return odg_move(doc, list, k, dx, dy);
}

static int do_resize(odg_edit *e, const odg_view *v, odg_doc *doc)
{
    const owf_dpage *p = odg_page(doc);
    const owf_dcell *c = owf_dpage_find(p, e->from), *parent;
    owf_dnum box[4], pbox[4];
    if (!resized(e, v, doc, box))
        return OWF_ERR_FORMAT;
    /* the core takes the place in the parent's terms: inside a group or container, from the parent's corner */
    parent = c->parent ? owf_dpage_find(p, c->parent) : NULL;
    if (parent && parent->kind == OWF_DCELL_EDGE)
        return OWF_ERR_FORMAT;
    if (parent && parent->kind == OWF_DCELL_VERTEX && owf_dcell_box(p, parent, pbox)) {
        box[0] -= pbox[0];
        box[1] -= pbox[1];
    }
    return odg_resize(doc, e->from, box[0], box[1], box[2], box[3]);
}

static int do_create(odg_edit *e, const odg_view *v, odg_doc *doc)
{
    owf_dnum x0 = odg_page_x(v, e->x0), y0 = odg_page_y(v, e->y0), x1 = odg_page_x(v, e->x1), y1 = odg_page_y(v, e->y1);
    owf_dnum x, y, w, h;
    char id[32];
    int text = e->tool == ODG_TOOL_TEXT, rc;
    const odg_stencil *st = e->tool == ODG_TOOL_SHAPE ? odg_stencil_at(e->shape) : NULL;
    if (e->tool == ODG_TOOL_SHAPE && !st)
        return OWF_ERR_FORMAT;
    if (!e->moved) {                     /* a click: a shape of the usual size with its corner there */
        x = x0;
        y = y0;
        w = OWF_DNUM(st ? st->width : text ? 60 : 120);
        h = OWF_DNUM(st ? st->height : text ? 30 : 60);
    } else {
        x = x0 < x1 ? x0 : x1;
        y = y0 < y1 ? y0 : y1;
        w = x0 < x1 ? x1 - x0 : x0 - x1;
        h = y0 < y1 ? y1 - y0 : y0 - y1;
        if (doc->grid > 0) {             /* the far corner on the grid too */
            owf_dnum sx = odg_snap(doc, x);
            w = odg_snap(doc, x + w) - sx;
            h = odg_snap(doc, y + h) - odg_snap(doc, y);
        }
        if (w < OWF_DNUM(1)) w = doc->grid > 0 ? doc->grid : OWF_DNUM(1);
        if (h < OWF_DNUM(1)) h = doc->grid > 0 ? doc->grid : OWF_DNUM(1);
    }
    rc = odg_add_shape(doc, x, y, w, h, st ? st->style : odg_tool_style(e->tool), text ? "Text" : "", id);
    if (rc != OWF_OK)
        return rc;
    snprintf(e->made, sizeof e->made, "%s", id);
    odg_edit_select(e, e->made, 0);
    e->tool = ODG_TOOL_SELECT;
    return OWF_OK;
}

static int do_connect(odg_edit *e, const odg_view *v, odg_doc *doc)
{
    char to[ODG_EDIT_ID] = "", id[32];
    owf_dpoint ls, lt;
    int rc;
    const owf_dpage *p = odg_page(doc);
    if (!odg_edit_hit(v, doc, e->x1, e->y1, 1, to))
        to[0] = 0;
    if (!e->moved && (!to[0] || !strcmp(to, e->from)))
        return OWF_OK;                   /* a click, not a line */
    if (to[0] && !strcmp(to, e->from))
        return OWF_OK;                   /* a shape to itself: no line to draw */
    ls.x = odg_page_x(v, e->x0);
    ls.y = odg_page_y(v, e->y0);
    lt.x = odg_page_x(v, e->x1);
    lt.y = odg_page_y(v, e->y1);
    rc = odg_add_edge(doc, is_shape(p, e->from) ? e->from : NULL, to[0] ? to : NULL, &ls, &lt,
                      odg_tool_style(ODG_TOOL_CONNECTOR), id);
    if (rc != OWF_OK)
        return rc;
    snprintf(e->made, sizeof e->made, "%s", id);
    odg_edit_select(e, e->made, 0);
    return OWF_OK;
}

static void do_lasso(odg_edit *e, const odg_view *v, const odg_doc *doc)
{
    const owf_dpage *p = odg_page(doc);
    owf_dnum x0 = odg_page_x(v, e->x0), y0 = odg_page_y(v, e->y0), x1 = odg_page_x(v, e->x1), y1 = odg_page_y(v, e->y1);
    owf_dnum l = x0 < x1 ? x0 : x1, r = x0 < x1 ? x1 : x0, t = y0 < y1 ? y0 : y1, b = y0 < y1 ? y1 : y0;
    int i;
    if (!e->moved)
        return;
    for (i = 0; i < p->ncells; i++) {
        const owf_dcell *c = &p->cells[i];
        owf_dnum box[4];
        if (!pickable(doc, c, 0) || !cell_bounds(p, c, box))
            continue;
        if (box[0] >= l && box[1] >= t && box[0] + box[2] <= r && box[1] + box[3] <= b)
            odg_edit_select(e, c->id, 1);
    }
}

int odg_edit_release(odg_edit *e, const odg_view *v, odg_doc *doc, long px, long py)
{
    odg_drag drag = e->drag;
    int rc = OWF_OK;
    odg_edit_motion(e, px, py);
    e->drag = ODG_DRAG_NONE;
    e->made[0] = 0;
    switch (drag) {
    case ODG_DRAG_MOVE: if (e->moved) rc = do_move(e, v, doc); break;
    case ODG_DRAG_RESIZE: if (e->moved) rc = do_resize(e, v, doc); break;
    case ODG_DRAG_CREATE: rc = do_create(e, v, doc); break;
    case ODG_DRAG_CONNECT: rc = do_connect(e, v, doc); break;
    case ODG_DRAG_LASSO: do_lasso(e, v, doc); break;
    default: break;
    }
    e->moved = 0;
    odg_edit_prune(e, doc);
    return rc;
}

int odg_edit_delete(odg_edit *e, odg_doc *doc)
{
    int rc;
    if (!e->n)
        return OWF_ERR_FORMAT;
    relink(e);
    rc = odg_delete(doc, e->list, e->n);
    odg_edit_prune(e, doc);
    return rc;
}

int odg_edit_nudge(odg_edit *e, odg_doc *doc, owf_dnum dx, owf_dnum dy)
{
    if (!e->n)
        return OWF_ERR_FORMAT;
    relink(e);
    return odg_move(doc, e->list, e->n, dx, dy);
}
