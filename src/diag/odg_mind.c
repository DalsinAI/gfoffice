/*
 * odg_mind.c: GFOpenDiag's mind-map mode on the editing core: the tree read from the page's cells, the edits
 * (child, sibling, delete, move, fold) as one Undo step each, and the layout. See include/opendiag_mind.h.
 * Design: amicradle-teams designs/GFOPENDIAG-20261113.md, section 3 and step D5.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "opendiag_mind.h"

#include <stdio.h>
#include <string.h>

#define NODE_W OWF_DNUM(100)
#define NODE_H OWF_DNUM(40)
#define ROOT_W OWF_DNUM(120)
#define ROOT_H OWF_DNUM(60)

/* One walk of a tree, breadth first, so a node's children sit together in their order and come after it. Static:
 * the walk is large for an Amiga task's stack, and the core is used by one task. */
static struct {
    int cell[ODG_MIND_MAX];              /* index in the page */
    int first[ODG_MIND_MAX], nkids[ODG_MIND_MAX];
    owf_dnum span[ODG_MIND_MAX];         /* the height the node's shown branch takes */
    int n, cut;                          /* cut: more nodes than ODG_MIND_MAX */
} walk;

static int is_on(const char *style, const char *key)
{
    char v[8];
    return owf_dstyle_get(style, key, v, sizeof v) && !strcmp(v, "1");
}

int odg_mind_is_node(const owf_dcell *c)
{
    return c && c->kind == OWF_DCELL_VERTEX && is_on(c->style, "treeFolding");
}

int odg_mind_folded(const owf_dcell *c)
{
    const char *v = c ? owf_dattrs_get(&c->extra, "collapsed") : NULL;
    return v && !strcmp(v, "1");
}

static int tree_edge(const owf_dpage *p, const owf_dcell *e)
{
    return e->kind == OWF_DCELL_EDGE && e->source && e->target && strcmp(e->source, e->target) &&
           odg_mind_is_node(owf_dpage_find(p, e->source)) && odg_mind_is_node(owf_dpage_find(p, e->target));
}

/* The branch into a node: the first tree connector that ends at it (a second one is a free connector). */
static int edge_in(const owf_dpage *p, const char *id)
{
    int i;
    for (i = 0; i < p->ncells; i++) {
        const owf_dcell *e = &p->cells[i];   /* the cheap test first: the lookups only for a connector to id */
        if (e->kind == OWF_DCELL_EDGE && e->target && !strcmp(e->target, id) && tree_edge(p, e))
            return i;
    }
    return -1;
}

/* The connector to child number k of the node (in page order), or -1. */
static int edge_to_child(const owf_dpage *p, const char *id, int k)
{
    int i;
    for (i = 0; i < p->ncells; i++) {
        const owf_dcell *e = &p->cells[i];
        if (e->kind == OWF_DCELL_EDGE && e->source && !strcmp(e->source, id) && tree_edge(p, e) &&
            edge_in(p, e->target) == i && k-- == 0)
            return i;
    }
    return -1;
}

const owf_dcell *odg_mind_parent(const odg_doc *doc, const char *id)
{
    const owf_dpage *p = odg_page(doc);
    int i;
    if (!id || !odg_mind_is_node(owf_dpage_find(p, id)))
        return NULL;
    i = edge_in(p, id);
    return i < 0 ? NULL : owf_dpage_find(p, p->cells[i].source);
}

int odg_mind_children(const odg_doc *doc, const char *id, const owf_dcell **out, int max)
{
    const owf_dpage *p = odg_page(doc);
    int i, n = 0;
    if (!id || !odg_mind_is_node(owf_dpage_find(p, id)))
        return 0;
    for (i = 0; i < p->ncells; i++) {
        const owf_dcell *e = &p->cells[i];
        if (e->kind == OWF_DCELL_EDGE && e->source && !strcmp(e->source, id) && tree_edge(p, e) &&
            edge_in(p, e->target) == i) {
            if (n < max && out)
                out[n] = owf_dpage_find(p, e->target);
            n++;
        }
    }
    return n;
}

const owf_dcell *odg_mind_root(const odg_doc *doc, const char *id)
{
    const owf_dcell *c = id ? owf_dpage_find(odg_page(doc), id) : NULL, *up;
    int depth = 0;
    if (!odg_mind_is_node(c))
        return NULL;
    while ((up = odg_mind_parent(doc, c->id)) != NULL) {
        if (++depth > ODG_MIND_DEPTH)
            return NULL;                 /* a loop of connectors, or a map deeper than a walk goes */
        c = up;
    }
    return c;
}

int odg_mind_both(const odg_doc *doc, const char *id)
{
    const owf_dpage *p = odg_page(doc);
    const owf_dcell *root = odg_mind_root(doc, id);
    int i;
    if (!root)
        return 0;
    for (i = 0; i < p->ncells; i++) {        /* every child, however many */
        const owf_dcell *e = &p->cells[i], *kid;
        if (e->kind != OWF_DCELL_EDGE || !e->source || strcmp(e->source, root->id) || !tree_edge(p, e) ||
            edge_in(p, e->target) != i)
            continue;
        kid = owf_dpage_find(p, e->target);
        if (kid->geo.x + kid->geo.width <= root->geo.x)
            return 1;
    }
    return 0;
}

static int listed(int cell)
{
    int i;
    for (i = 0; i < walk.n; i++)
        if (walk.cell[i] == cell)
            return 1;
    return 0;
}

/* The tree from the node at index top into walk; folded branches too when all is 1. */
static void walk_from(const owf_dpage *p, int top, int all)
{
    int i, k;
    walk.n = 1;
    walk.cut = 0;
    walk.cell[0] = top;
    for (i = 0; i < walk.n; i++) {
        const owf_dcell *c = &p->cells[walk.cell[i]];
        walk.first[i] = walk.n;
        walk.nkids[i] = 0;
        if (!all && odg_mind_folded(c))
            continue;
        for (k = 0; k < p->ncells; k++) {
            const owf_dcell *e = &p->cells[k];
            int t;
            if (e->kind != OWF_DCELL_EDGE || !e->source || strcmp(e->source, c->id) || !tree_edge(p, e) ||
                edge_in(p, e->target) != k)
                continue;
            t = owf_dpage_index(p, e->target);
            if (t < 0 || listed(t))
                continue;                /* a loop back up the tree */
            if (walk.n == ODG_MIND_MAX) {
                walk.cut = 1;
                break;
            }
            walk.cell[walk.n++] = t;
            walk.nkids[i]++;
        }
    }
}

/* Folding shown: a node under a folded one is hidden, and so is its branch connector. Only the branch of open
 * (the index of a node just unfolded, else -1) is shown again, so a node or connector hidden by hand elsewhere
 * stays hidden through the map's edits. */
static int show_folds(owf_dpage *p, int top, int open)
{
    static unsigned char hidden[ODG_MIND_MAX], under[ODG_MIND_MAX];
    int i, k;
    walk_from(p, top, 1);
    hidden[0] = 0;
    under[0] = walk.cell[0] == open;
    for (i = 0; i < walk.n; i++) {
        int fold = hidden[i] || odg_mind_folded(&p->cells[walk.cell[i]]);
        for (k = walk.first[i]; k < walk.first[i] + walk.nkids[i]; k++) {
            owf_dcell *c = &p->cells[walk.cell[k]];
            int e = edge_in(p, c->id);
            hidden[k] = (unsigned char)fold;
            under[k] = (unsigned char)(under[i] || walk.cell[k] == open);
            if (!fold && !under[i])
                continue;                /* shown already, or hidden by hand: left as it is */
            if (owf_dattrs_set(&c->extra, "visible", fold ? "0" : NULL) != OWF_OK ||
                (e >= 0 && owf_dattrs_set(&p->cells[e].extra, "visible", fold ? "0" : NULL) != OWF_OK))
                return OWF_ERR_MEMORY;
        }
    }
    return OWF_OK;
}

/* The children from walk position from, count of them, put beside the node at position at on side (1 right, -1
 * left), centred on it, one under the other. A locked node (or one on a locked layer) keeps its place, as the mouse
 * leaves it alone; its branch is still laid out from where it is. */
static void place(const odg_doc *doc, owf_dpage *p, int at, int from, int count, int side)
{
    const owf_dcell *c = &p->cells[walk.cell[at]];
    owf_dnum total = 0, y;
    int k;
    for (k = from; k < from + count; k++)
        total += walk.span[k] + (k > from ? ODG_MIND_VGAP : 0);
    y = c->geo.y + c->geo.height / 2 - total / 2;
    for (k = from; k < from + count; k++) {
        owf_dcell *kid = &p->cells[walk.cell[k]];
        c = &p->cells[walk.cell[at]];
        if (odg_cell_locked(doc, kid)) {
            y += walk.span[k] + ODG_MIND_VGAP;
            continue;
        }
        kid->geo.y = y + (walk.span[k] - kid->geo.height) / 2;
        kid->geo.x = side > 0 ? c->geo.x + c->geo.width + ODG_MIND_HGAP : c->geo.x - ODG_MIND_HGAP - kid->geo.width;
        y += walk.span[k] + ODG_MIND_VGAP;
    }
}

/* The tree from the root at index top laid out: the root stays where it is; its children go to its right (both:
 * the first half right, the rest left), each branch on the side its first node is on. */
static void lay_out(const odg_doc *doc, owf_dpage *p, int top, int both)
{
    static signed char side[ODG_MIND_MAX];
    int i, k;
    walk_from(p, top, 0);
    for (i = walk.n - 1; i >= 0; i--) {
        owf_dnum h = p->cells[walk.cell[i]].geo.height, sum = 0;
        for (k = walk.first[i]; k < walk.first[i] + walk.nkids[i]; k++)
            sum += walk.span[k] + (k > walk.first[i] ? ODG_MIND_VGAP : 0);
        walk.span[i] = sum > h ? sum : h;
    }
    side[0] = 1;
    for (i = 0; i < walk.n; i++) {
        int f = walk.first[i], n = walk.nkids[i], right = n;
        if (!n)
            continue;
        if (i == 0 && both)
            right = (n + 1) / 2;
        for (k = f; k < f + n; k++)
            side[k] = (signed char)(k - f < right ? side[i] : -side[i]);
        place(doc, p, i, f, right, side[i]);
        if (right < n)
            place(doc, p, i, f + right, n - right, -side[i]);
    }
    for (i = 1; i < walk.n; i++) {       /* the branches drawn from box to box again */
        int e = edge_in(p, p->cells[walk.cell[i]].id);
        if (e >= 0 && !odg_cell_locked(doc, &p->cells[e]) && !odg_cell_locked(doc, &p->cells[walk.cell[i]]))
            p->cells[e].geo.npoints = 0;
    }
}

/* After an edit: folding shown (openid: a node just unfolded, or NULL) and the map rootid is the root of laid out
 * again. */
static int finish(odg_doc *doc, const char *rootid, int both, const char *openid)
{
    owf_dpage *p = odg_page(doc);
    int top = rootid ? owf_dpage_index(p, rootid) : -1, r;
    if (top < 0)
        return OWF_OK;                   /* the whole map went */
    if ((r = show_folds(p, top, openid ? owf_dpage_index(p, openid) : -1)) != OWF_OK)
        return r;
    lay_out(doc, p, top, both);
    return OWF_OK;
}

static char *dup(const char *s)
{
    size_t n = s ? strlen(s) + 1 : 1;
    char *d = (char *)owf_malloc(n);
    if (d)
        memcpy(d, s ? s : "", n);
    return d;
}

/* id into out (ODG_MIND_ID bytes); 0, and out left empty, when it is too long: a cut id could name another cell
 * (diagrams.net's ids of one file share their first 20 characters). */
static int put_id(char *out, const char *id)
{
    if (!out)
        return 1;
    if (strlen(id) >= ODG_MIND_ID) {
        out[0] = 0;
        return 0;
    }
    memcpy(out, id, strlen(id) + 1);
    return 1;
}

/* The node id when the mouse or a script may edit it: a node, shown and not locked. */
static const owf_dcell *open_node(const odg_doc *doc, const char *id)
{
    const owf_dpage *p = odg_page(doc);
    const owf_dcell *c = id ? owf_dpage_find(p, id) : NULL;
    if (!odg_mind_is_node(c) || !owf_dcell_shown(p, c) || odg_cell_locked(doc, c))
        return NULL;
    return c;
}

static owf_dcell *new_node(owf_dpage *p, const char *in, owf_dnum x, owf_dnum y, owf_dnum w, owf_dnum h,
                           const char *style, const char *text)
{
    owf_dcell *c = owf_dpage_add(p, NULL, in, OWF_DCELL_VERTEX);
    if (!c)
        return NULL;
    c->geo.flags = OWF_DGEO_HAS;
    c->geo.x = x;
    c->geo.y = y;
    c->geo.width = w;
    c->geo.height = h;
    if (owf_dcell_set(&c->style, style) != OWF_OK || owf_dcell_set(&c->value, text) != OWF_OK)
        return NULL;
    return c;
}

static owf_dcell *new_branch(owf_dpage *p, const char *in, const char *from, const char *to)
{
    owf_dcell *e = owf_dpage_add(p, NULL, in, OWF_DCELL_EDGE);
    if (!e)
        return NULL;
    e->geo.flags = OWF_DGEO_HAS | OWF_DGEO_RELATIVE;
    if (owf_dcell_set(&e->style, ODG_MIND_EDGE_STYLE) != OWF_OK || owf_dcell_set(&e->source, from) != OWF_OK ||
        owf_dcell_set(&e->target, to) != OWF_OK)
        return NULL;
    return e;
}

/* The cell at from put at to (from > to), the ones between moving up one. */
static void move_cell(owf_dpage *p, int from, int to)
{
    owf_dcell keep = p->cells[from];
    memmove(&p->cells[to + 1], &p->cells[to], (size_t)(from - to) * sizeof *p->cells);
    p->cells[to] = keep;
}

int odg_mind_new(odg_doc *doc, owf_dnum x, owf_dnum y, const char *text, char *id)
{
    int i = odg_layer_current(doc), r = OWF_OK;
    char *in;
    owf_dcell *c;
    if (i < 0 || !odg_layer_shown(doc, i) || odg_layer_locked(doc, i))
        return OWF_ERR_FORMAT;
    if (!(in = dup(odg_layer_at(doc, i)->id)))
        return OWF_ERR_MEMORY;
    if (odg_begin(doc) != OWF_OK) {
        owf_free(in);
        return OWF_ERR_MEMORY;
    }
    c = new_node(odg_page(doc), in, odg_snap(doc, x), odg_snap(doc, y), ROOT_W, ROOT_H, ODG_MIND_ROOT_STYLE, text);
    if (!c)
        r = OWF_ERR_MEMORY;
    else
        put_id(id, c->id);
    owf_free(in);
    return odg_end(doc, r);
}

/* A new child of parent (parent is a node), at the end of its row or (next >= 0) put before the connector at
 * index next of the page. */
static int add_under(odg_doc *doc, const char *parent, int next, const char *text, int both, char *id)
{
    const owf_dcell *pc = open_node(doc, parent), *root;
    char *pid, *in, *rootid, kid[ODG_MIND_ID];
    owf_dpage *p;
    owf_dcell *c;
    int r = OWF_OK, unfold = 0;
    if (!pc || !(root = odg_mind_root(doc, parent)))
        return OWF_ERR_FORMAT;
    pid = dup(pc->id);
    in = dup(pc->parent);
    rootid = dup(root->id);
    if (!pid || !in || !rootid || odg_begin(doc) != OWF_OK) {
        owf_free(pid); owf_free(in); owf_free(rootid);
        return OWF_ERR_MEMORY;
    }
    p = odg_page(doc);
    pc = owf_dpage_find(p, pid);
    unfold = odg_mind_folded(pc);
    if (unfold && owf_dattrs_set(&((owf_dcell *)pc)->extra, "collapsed", NULL) != OWF_OK)
        r = OWF_ERR_MEMORY;
    else if (!(c = new_node(p, in, pc->geo.x + pc->geo.width + ODG_MIND_HGAP, pc->geo.y, NODE_W, NODE_H,
                            ODG_MIND_NODE_STYLE, text)))
        r = OWF_ERR_MEMORY;
    else {
        snprintf(kid, sizeof kid, "%s", c->id);
        if (!new_branch(p, in, pid, kid))
            r = OWF_ERR_MEMORY;
        else {
            if (next >= 0)
                move_cell(p, p->ncells - 1, next);
            put_id(id, kid);
            r = finish(doc, rootid, both, unfold ? pid : NULL);
        }
    }
    owf_free(pid); owf_free(in); owf_free(rootid);
    return odg_end(doc, r);
}

int odg_mind_add_child(odg_doc *doc, const char *parent, const char *text, int both, char *id)
{
    return add_under(doc, parent, -1, text, both, id);
}

int odg_mind_add_sibling(odg_doc *doc, const char *node, int above, const char *text, int both, char *id)
{
    const owf_dpage *p = odg_page(doc);
    const owf_dcell *parent;
    int at;
    if (!open_node(doc, node) || !(parent = odg_mind_parent(doc, node)))
        return OWF_ERR_FORMAT;
    at = edge_in(p, node);
    return add_under(doc, parent->id, above ? at : at + 1, text, both, id);
}

int odg_mind_delete(odg_doc *doc, const char *node, int both)
{
    const owf_dpage *p = odg_page(doc);
    const owf_dcell *c = open_node(doc, node), *root;
    char **ids, *rootid;
    int i, n, r = OWF_OK;
    if (!c || !(root = odg_mind_root(doc, node)))
        return OWF_ERR_FORMAT;
    walk_from(p, owf_dpage_index(p, c->id), 1);
    if (walk.cut)
        return OWF_ERR_FORMAT;           /* a branch bigger than a walk: not cut in two */
    n = walk.n;
    for (i = 0; i < n; i++)
        if (odg_cell_locked(doc, &p->cells[walk.cell[i]]))
            return OWF_ERR_FORMAT;       /* a locked node in the branch: the branch stays */
    ids = (char **)owf_malloc((size_t)n * sizeof *ids);
    rootid = root == c ? NULL : dup(root->id);
    if (!ids || (root != c && !rootid)) {
        owf_free(ids); owf_free(rootid);
        return OWF_ERR_MEMORY;
    }
    for (i = 0; i < n; i++)
        ids[i] = NULL;
    for (i = 0; i < n && r == OWF_OK; i++)
        if (!(ids[i] = dup(p->cells[walk.cell[i]].id)))
            r = OWF_ERR_MEMORY;
    if (r == OWF_OK && odg_begin(doc) != OWF_OK)
        r = OWF_ERR_MEMORY;
    else if (r == OWF_OK) {
        for (i = 0; i < n; i++)
            owf_dpage_remove(odg_page(doc), ids[i]);
        r = odg_end(doc, finish(doc, rootid, both, NULL));
    }
    for (i = 0; i < n; i++)
        owf_free(ids[i]);
    owf_free(ids);
    owf_free(rootid);
    return r;
}

int odg_mind_move(odg_doc *doc, const char *node, int by, int both)
{
    const owf_dcell *parent, *root;
    const owf_dpage *p = odg_page(doc);
    char *rootid;
    int k, n, at, other, r;
    if (!open_node(doc, node) || (by != 1 && by != -1) || !(parent = odg_mind_parent(doc, node)) ||
        !(root = odg_mind_root(doc, node)))
        return OWF_ERR_FORMAT;
    n = odg_mind_children(doc, parent->id, NULL, 0);
    at = edge_in(p, node);
    for (k = 0; k < n && edge_to_child(p, parent->id, k) != at; k++)
        ;
    if (k + by < 0 || k + by >= n)
        return OWF_ERR_FORMAT;
    other = edge_to_child(p, parent->id, k + by);
    rootid = dup(root->id);
    if (!rootid || odg_begin(doc) != OWF_OK) {
        owf_free(rootid);
        return OWF_ERR_MEMORY;
    }
    {
        owf_dpage *w = odg_page(doc);        /* the two branch connectors trade places in the page */
        owf_dcell keep = w->cells[at];
        w->cells[at] = w->cells[other];
        w->cells[other] = keep;
    }
    r = odg_end(doc, finish(doc, rootid, both, NULL));
    owf_free(rootid);
    return r;
}

int odg_mind_fold(odg_doc *doc, const char *node, int folded, int both)
{
    const owf_dcell *c = open_node(doc, node), *root;
    char *id, *rootid;
    int r;
    if (!c || !(root = odg_mind_root(doc, node)) || !odg_mind_children(doc, node, NULL, 0))
        return OWF_ERR_FORMAT;
    id = dup(c->id);
    rootid = dup(root->id);
    if (!id || !rootid || odg_begin(doc) != OWF_OK) {
        owf_free(id); owf_free(rootid);
        return OWF_ERR_MEMORY;
    }
    r = owf_dattrs_set(&owf_dpage_find(odg_page(doc), id)->extra, "collapsed", folded ? "1" : NULL);
    if (r == OWF_OK)
        r = finish(doc, rootid, both, folded ? NULL : id);
    r = odg_end(doc, r);
    owf_free(id);
    owf_free(rootid);
    return r;
}

int odg_mind_layout(odg_doc *doc, const char *node, int both)
{
    const owf_dcell *root = odg_mind_root(doc, node);
    char *rootid;
    int r;
    if (!root || !open_node(doc, node))
        return OWF_ERR_FORMAT;
    if (!(rootid = dup(root->id)) || odg_begin(doc) != OWF_OK) {
        owf_free(rootid);
        return OWF_ERR_MEMORY;
    }
    r = odg_end(doc, finish(doc, rootid, both, NULL));
    owf_free(rootid);
    return r;
}

int odg_mind_step(const odg_doc *doc, const char *id, odg_mind_dir dir, char *out)
{
    const owf_dpage *p = odg_page(doc);
    const owf_dcell *c = id ? owf_dpage_find(p, id) : NULL, *parent, *got = NULL;
    int k, n, e;
    if (!odg_mind_is_node(c))
        return 0;
    parent = odg_mind_parent(doc, id);
    if (dir == ODG_MIND_PARENT)
        got = parent;
    else if (dir == ODG_MIND_CHILD) {
        if (!odg_mind_folded(c) && (e = edge_to_child(p, id, 0)) >= 0)
            got = owf_dpage_find(p, p->cells[e].target);
    } else if (parent) {
        n = odg_mind_children(doc, parent->id, NULL, 0);
        e = edge_in(p, id);
        for (k = 0; k < n && edge_to_child(p, parent->id, k) != e; k++)
            ;
        k += dir == ODG_MIND_NEXT ? 1 : -1;
        if (k >= 0 && k < n && (e = edge_to_child(p, parent->id, k)) >= 0)
            got = owf_dpage_find(p, p->cells[e].target);
    }
    if (!got)
        return 0;
    return put_id(out, got->id);
}

const char *odg_mind_why(const odg_doc *doc, const char *node, odg_mind_key key)
{
    const owf_dcell *c = node ? owf_dpage_find(odg_page(doc), node) : NULL;
    if (!odg_mind_is_node(c))
        return "no map node by that id";
    if (strlen(node) >= ODG_MIND_ID)
        return "its id is longer than 63 characters";
    if (!odg_mind_root(doc, node))
        return "its map loops back on itself";
    if ((key == ODG_MKEY_SIBLING || key == ODG_MKEY_SIBLING_ABOVE) && !odg_mind_parent(doc, node))
        return "a map's root has no siblings";
    if ((key == ODG_MKEY_MOVE_UP || key == ODG_MKEY_MOVE_DOWN) && odg_mind_parent(doc, node))
        return "it is at the end of its row already";
    if ((key == ODG_MKEY_FOLD || key == ODG_MKEY_FOLD_ON || key == ODG_MKEY_FOLD_OFF) &&
        !odg_mind_children(doc, node, NULL, 0))
        return "it has no children";
    return "not a map node that can be changed (hidden or locked?)";
}

/* 1 when c's middle is left of the middle of its map's root: the left side of a map laid out both ways. */
static int on_left(const odg_doc *doc, const owf_dcell *c, const owf_dcell *root)
{
    owf_dnum a[4], b[4];
    if (!owf_dcell_box(odg_page(doc), c, a) || !owf_dcell_box(odg_page(doc), root, b))
        return 0;
    return a[0] + a[2] / 2 < b[0] + b[2] / 2;
}

/* The arrow keys: towards the root is back (left on the right side, right on the left side). The root steps to
 * its first open child on that side. */
static void step_side(const odg_doc *doc, const char *node, int right, char *out)
{
    const owf_dcell *c = owf_dpage_find(odg_page(doc), node), *root = odg_mind_root(doc, node);
    if (!root)
        return;
    if (c == root) {
        int i, n = odg_mind_folded(c) ? 0 : odg_mind_children(doc, node, NULL, 0);
        const owf_dcell **kids = n ? (const owf_dcell **)owf_malloc((size_t)n * sizeof *kids) : NULL;
        if (!kids)
            return;
        odg_mind_children(doc, node, kids, n);
        for (i = 0; i < n; i++)
            if (on_left(doc, kids[i], root) != right) {
                put_id(out, kids[i]->id);
                break;
            }
        owf_free(kids);
        return;
    }
    odg_mind_step(doc, node, on_left(doc, c, root) == right ? ODG_MIND_PARENT : ODG_MIND_CHILD, out);
}

int odg_mind_key_do(odg_doc *doc, const char *node, odg_mind_key key, char *out, const char **why)
{
    const owf_dcell *c = node ? owf_dpage_find(odg_page(doc), node) : NULL;
    char made[ODG_MIND_ID] = "";
    int rc = OWF_OK, both;
    out[0] = 0;
    if (!odg_mind_is_node(c)) {
        if (why)
            *why = odg_mind_why(doc, node, key);
        return OWF_ERR_FORMAT;
    }
    if (!put_id(out, node)) {           /* never a cut id */
        if (why)
            *why = odg_mind_why(doc, node, key);
        return OWF_ERR_FORMAT;
    }
    both = odg_mind_both(doc, node);
    switch (key) {
    case ODG_MKEY_CHILD: rc = odg_mind_add_child(doc, node, "", both, made); break;
    case ODG_MKEY_SIBLING:
    case ODG_MKEY_SIBLING_ABOVE:
        rc = odg_mind_add_sibling(doc, node, key == ODG_MKEY_SIBLING_ABOVE, "", both, made);
        break;
    case ODG_MKEY_LEFT:
    case ODG_MKEY_RIGHT: step_side(doc, node, key == ODG_MKEY_RIGHT, made); break;   /* into made: out keeps node */
    case ODG_MKEY_UP:
    case ODG_MKEY_DOWN: odg_mind_step(doc, node, key == ODG_MKEY_UP ? ODG_MIND_PREV : ODG_MIND_NEXT, made); break;
    case ODG_MKEY_FOLD:
    case ODG_MKEY_FOLD_ON:
    case ODG_MKEY_FOLD_OFF:
        rc = odg_mind_fold(doc, node, key == ODG_MKEY_FOLD ? !odg_mind_folded(c) : key == ODG_MKEY_FOLD_ON, both);
        break;
    case ODG_MKEY_MOVE_UP:
    case ODG_MKEY_MOVE_DOWN: rc = odg_mind_move(doc, node, key == ODG_MKEY_MOVE_UP ? -1 : 1, both); break;
    case ODG_MKEY_DELETE:                 /* the next one picked before the branch goes */
        if (!odg_mind_step(doc, node, ODG_MIND_NEXT, made) && !odg_mind_step(doc, node, ODG_MIND_PREV, made) &&
            !odg_mind_step(doc, node, ODG_MIND_PARENT, made))
            made[0] = 0;
        rc = odg_mind_delete(doc, node, both);
        if (rc == OWF_OK)
            put_id(out, made);
        made[0] = 0;
        break;
    case ODG_MKEY_LAYOUT: rc = odg_mind_layout(doc, node, both); break;
    }
    if (rc != OWF_OK) {
        if (why)
            *why = odg_mind_why(doc, node, key);
        return rc;
    }
    if (made[0])
        put_id(out, made);
    return OWF_OK;
}

int odg_mind_whole(const odg_doc *doc, const char *id)
{
    const owf_dpage *p = odg_page(doc);
    const owf_dcell *root = odg_mind_root(doc, id);
    if (!root)
        return 0;
    walk_from(p, owf_dpage_index(p, root->id), 1);
    return !walk.cut;
}
