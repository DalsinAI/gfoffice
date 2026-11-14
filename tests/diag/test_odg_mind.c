/*
 * GFOpenDiag's mind-map mode on the PC (src/diag/odg_mind.c): the tree read from the cells, each edit one Undo
 * step, folding, the layout (places worked out by hand below), the arrow keys, and maps that loop or sit on a
 * locked layer. Run under ASan and UBSan by make check.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <stdio.h>
#include <string.h>
#include "opendiag_mind.h"

static int fails, checks;

#define CHECK(cond) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_N(got, want) do { long g_ = (long)(got), w_ = (long)(want); checks++; if (g_ != w_) { fails++; \
    printf("FAIL %s:%d: %s is %ld, want %ld\n", __FILE__, __LINE__, #got, g_, w_); } } while (0)
#define CHECK_S(got, want) do { const char *g_ = (got), *w_ = (want); checks++; if (!g_ || strcmp(g_, w_) != 0) { \
    fails++; printf("FAIL %s:%d: %s is \"%s\", want \"%s\"\n", __FILE__, __LINE__, #got, g_ ? g_ : "(null)", w_); } } while (0)

#define PX(n) OWF_DNUM(n)

static owf_diagram d;
static odg_doc doc;

static void fresh(void)
{
    owf_diagram_init(&d);
    owf_diagram_add_page(&d, "p", "Page-1");
    odg_init(&doc, &d, 0);
}

static void done(void)
{
    odg_free(&doc);
    owf_diagram_free(&d);
}

static owf_dcell *cell(const char *id)
{
    return owf_dpage_find(odg_page(&doc), id);
}

static int shown(const char *id)
{
    const char *v = cell(id) ? owf_dattrs_get(&cell(id)->extra, "visible") : NULL;
    return !(v && !strcmp(v, "0"));
}

/* The id of the branch connector into node. */
static const char *branch(const char *node)
{
    const owf_dpage *p = odg_page(&doc);
    int i;
    for (i = 0; i < p->ncells; i++)
        if (p->cells[i].kind == OWF_DCELL_EDGE && p->cells[i].target && !strcmp(p->cells[i].target, node))
            return p->cells[i].id;
    return "";
}

static void check_at(const char *id, long x, long y)
{
    CHECK_N(cell(id)->geo.x, PX(x));
    CHECK_N(cell(id)->geo.y, PX(y));
}

static void test_build_and_layout(void)
{
    char r[24], a[24], b[24], a1[24], a2[24], a0[24], got[24];
    const owf_dcell *kids[4];
    int steps;
    fresh();
    CHECK_N(odg_mind_new(&doc, PX(100), PX(200), "Plan", r), OWF_OK);
    CHECK(odg_mind_is_node(cell(r)));
    CHECK_N(cell(r)->geo.width, PX(120));
    CHECK(odg_mind_root(&doc, r) == cell(r));
    CHECK(odg_mind_parent(&doc, r) == NULL);
    CHECK_N(odg_mind_children(&doc, r, NULL, 0), 0);
    steps = doc.nundo;
    CHECK_N(odg_mind_add_child(&doc, r, "Alpha", 0, a), OWF_OK);
    CHECK_N(doc.nundo, steps + 1);                        /* a node and its branch: one step */
    CHECK_S(cell(a)->value, "Alpha");
    CHECK(odg_mind_parent(&doc, a) == cell(r));
    check_at(a, 260, 210);                                /* right of the root (100 + 120 + 40), centred on it */
    CHECK_N(odg_mind_add_child(&doc, r, "Beta", 0, b), OWF_OK);
    check_at(a, 260, 185);                                /* 40 + 10 + 40 = 90 high, centred on 230 */
    check_at(b, 260, 235);
    CHECK_N(odg_mind_add_child(&doc, a, "One", 0, a1), OWF_OK);
    check_at(a1, 400, 185);                               /* 260 + 100 + 40 */
    CHECK_N(odg_mind_add_sibling(&doc, a1, 0, "Two", 0, a2), OWF_OK);
    check_at(a, 260, 185);                                /* Alpha's branch is 90: the root's 140, from 160 */
    check_at(b, 260, 260);
    check_at(a1, 400, 160);
    check_at(a2, 400, 210);
    CHECK_N(odg_mind_add_sibling(&doc, a1, 1, "Zero", 0, a0), OWF_OK);   /* above One */
    CHECK_N(odg_mind_children(&doc, a, kids, 4), 3);
    CHECK(kids[0] == cell(a0) && kids[1] == cell(a1) && kids[2] == cell(a2));
    CHECK_N(odg_mind_add_sibling(&doc, r, 0, "No", 0, got), OWF_ERR_FORMAT);   /* a root has no siblings */
    CHECK_S(cell(branch(a))->style, ODG_MIND_EDGE_STYLE);

    /* The arrow keys. */
    CHECK(odg_mind_step(&doc, r, ODG_MIND_CHILD, got)); CHECK_S(got, a);
    CHECK(odg_mind_step(&doc, a, ODG_MIND_NEXT, got)); CHECK_S(got, b);
    CHECK(!odg_mind_step(&doc, b, ODG_MIND_NEXT, got));
    CHECK(odg_mind_step(&doc, b, ODG_MIND_PREV, got)); CHECK_S(got, a);
    CHECK(odg_mind_step(&doc, a2, ODG_MIND_PARENT, got)); CHECK_S(got, a);
    CHECK(!odg_mind_step(&doc, r, ODG_MIND_PARENT, got));
    CHECK(!odg_mind_step(&doc, r, ODG_MIND_PREV, got));

    /* Both sides: the root's first half right, the rest left (100 - 40 - 100 = -40). */
    CHECK(!odg_mind_both(&doc, a2));
    CHECK_N(odg_mind_layout(&doc, a2, 1), OWF_OK);
    CHECK(odg_mind_both(&doc, a2));                       /* read back from where the root's children are */
    CHECK(!odg_mind_both(&doc, "nope"));
    CHECK_N(cell(a)->geo.x, PX(260));
    check_at(b, -40, 210);
    CHECK_N(cell(a1)->geo.x, PX(400));                    /* a branch keeps its side */
    done();
}

static void test_edits(void)
{
    char r[24], a[24], b[24], a1[24], a2[24], got[24];
    const owf_dcell *kids[4];
    int steps, n;
    fresh();
    odg_mind_new(&doc, PX(100), PX(200), "Plan", r);
    odg_mind_add_child(&doc, r, "Alpha", 0, a);
    odg_mind_add_child(&doc, r, "Beta", 0, b);
    odg_mind_add_child(&doc, a, "One", 0, a1);
    odg_mind_add_child(&doc, a, "Two", 0, a2);

    /* Move among siblings: Beta up is first; up again is the end of the row. */
    CHECK_N(odg_mind_move(&doc, b, -1, 0), OWF_OK);
    CHECK_N(odg_mind_children(&doc, r, kids, 4), 2);
    CHECK(kids[0] == cell(b) && kids[1] == cell(a));
    CHECK(cell(b)->geo.y < cell(a)->geo.y);
    CHECK_N(odg_mind_move(&doc, b, -1, 0), OWF_ERR_FORMAT);
    CHECK_N(odg_mind_move(&doc, r, 1, 0), OWF_ERR_FORMAT);   /* the root has no row */
    CHECK_N(odg_undo(&doc), OWF_OK);
    CHECK_N(odg_mind_children(&doc, r, kids, 4), 2);
    CHECK(kids[0] == cell(a));

    /* Fold: the branch and its connectors hidden, the layout as if it had no children; unfold brings it back. */
    steps = doc.nundo;
    CHECK_N(odg_mind_fold(&doc, a, 1, 0), OWF_OK);
    CHECK_N(doc.nundo, steps + 1);
    CHECK(odg_mind_folded(cell(a)));
    CHECK(!shown(a1) && !shown(a2) && !shown(branch(a1)));
    CHECK(shown(a) && shown(b));
    check_at(a, 260, 185);                                /* Alpha 40 high again: as with two leaves */
    CHECK(!odg_mind_step(&doc, a, ODG_MIND_CHILD, got));  /* not stepped into */
    CHECK_N(odg_mind_fold(&doc, b, 1, 0), OWF_ERR_FORMAT);   /* no children */
    CHECK_N(odg_mind_add_child(&doc, a, "Three", 0, got), OWF_OK);   /* adding unfolds */
    CHECK(!odg_mind_folded(cell(a)));
    CHECK(shown(a1) && shown(a2) && shown(got));
    CHECK_N(odg_undo(&doc), OWF_OK);
    CHECK_N(odg_mind_fold(&doc, a, 0, 0), OWF_OK);
    CHECK(shown(a1) && shown(branch(a1)));

    /* A node hidden by hand stays hidden through the map's edits; only an unfolded branch is shown again. */
    owf_dattrs_set(&cell(b)->extra, "visible", "0");
    CHECK_N(odg_mind_add_child(&doc, a, "Four", 0, got), OWF_OK);
    CHECK(!shown(b));
    CHECK_N(odg_mind_layout(&doc, a, 0), OWF_OK);
    CHECK(!shown(b));
    CHECK_N(odg_undo(&doc), OWF_OK);
    CHECK_N(odg_undo(&doc), OWF_OK);
    owf_dattrs_set(&cell(b)->extra, "visible", NULL);

    /* Delete: the whole branch and its connectors, one step; Undo puts it back. */
    n = odg_page(&doc)->ncells;
    steps = doc.nundo;
    CHECK_N(odg_mind_delete(&doc, a, 0), OWF_OK);
    CHECK_N(doc.nundo, steps + 1);
    CHECK(!cell(a) && !cell(a1) && !cell(a2));
    CHECK_N(odg_page(&doc)->ncells, n - 6);               /* three nodes, three branches */
    check_at(b, 260, 210);                                /* Beta alone: laid out again */
    CHECK_N(odg_undo(&doc), OWF_OK);
    CHECK(cell(a) && cell(a1) && cell(a2));
    CHECK_N(odg_mind_delete(&doc, r, 0), OWF_OK);          /* the root: the whole map */
    CHECK_N(odg_page(&doc)->ncells, 2);                    /* the page's root and layer */
    CHECK_N(odg_mind_delete(&doc, "nope", 0), OWF_ERR_FORMAT);
    done();
}

/* Free cells beside the map are left alone; a second connector into a node is not a branch; a loop of
 * connectors and a locked layer are refused without hanging. */
static void test_odd_maps(void)
{
    char r[24], a[24], b[24], free_shape[24], e[24];
    const owf_dcell *kids[4];
    fresh();
    odg_mind_new(&doc, PX(100), PX(200), "Plan", r);
    odg_mind_add_child(&doc, r, "Alpha", 0, a);
    odg_mind_add_child(&doc, r, "Beta", 0, b);
    odg_add_shape(&doc, PX(500), PX(500), PX(50), PX(50), "", "free", free_shape);
    CHECK(!odg_mind_is_node(cell(free_shape)));
    CHECK_N(odg_add_edge(&doc, a, b, NULL, NULL, "", e), OWF_OK);   /* Alpha to Beta, beside the tree */
    CHECK_N(odg_mind_children(&doc, a, kids, 4), 0);
    CHECK(odg_mind_parent(&doc, b) == cell(r));
    CHECK_N(odg_mind_layout(&doc, r, 0), OWF_OK);
    check_at(free_shape, 500, 500);
    CHECK_N(odg_mind_layout(&doc, free_shape, 0), OWF_ERR_FORMAT);   /* not a node */
    CHECK_N(odg_mind_add_child(&doc, free_shape, "x", 0, NULL), OWF_ERR_FORMAT);

    CHECK_N(odg_layer_lock(&doc, 0, 1), OWF_OK);           /* locked: nothing changes */
    CHECK_N(odg_mind_add_child(&doc, a, "x", 0, NULL), OWF_ERR_FORMAT);
    CHECK_N(odg_mind_delete(&doc, a, 0), OWF_ERR_FORMAT);
    CHECK_N(odg_mind_new(&doc, 0, 0, "x", NULL), OWF_ERR_FORMAT);
    CHECK_N(odg_layer_lock(&doc, 0, 0), OWF_OK);
    {                                                     /* a child on a locked layer: its branch stays */
        int at;
        char a1[24];
        odg_mind_add_child(&doc, a, "One", 0, a1);
        CHECK_N(odg_layer_add(&doc, "Top", &at), OWF_OK);
        owf_dcell_set(&cell(a1)->parent, odg_layer_at(&doc, 1)->id);
        CHECK_N(odg_layer_lock(&doc, 1, 1), OWF_OK);
        CHECK_N(odg_mind_delete(&doc, a, 0), OWF_ERR_FORMAT);
        CHECK(cell(a) && cell(a1));
        CHECK_N(odg_layer_lock(&doc, 1, 0), OWF_OK);
        CHECK_N(odg_mind_delete(&doc, a, 0), OWF_OK);
        CHECK(!cell(a) && !cell(a1));
    }
    done();

    fresh();                                              /* a loop: Root -> A -> Root */
    odg_mind_new(&doc, PX(100), PX(200), "Plan", r);
    odg_mind_add_child(&doc, r, "Alpha", 0, a);
    {
        owf_dpage *p = odg_page(&doc);
        int i = owf_dpage_index(p, branch(a));
        owf_dcell *loop = owf_dpage_add(p, "loop", p->cells[i].parent, OWF_DCELL_EDGE);
        owf_dcell_set(&loop->source, a);
        owf_dcell_set(&loop->target, r);
    }
    CHECK(odg_mind_root(&doc, a) == NULL);
    CHECK_N(odg_mind_add_child(&doc, a, "x", 0, NULL), OWF_ERR_FORMAT);
    CHECK_N(odg_mind_layout(&doc, a, 0), OWF_ERR_FORMAT);
    done();
}

/* The window's keys (odg_mind_key_do): what each does and the node it leaves selected; the arrows on both sides of
 * a map laid out both ways; and the reasons a key gives when it can't be done. */
static void test_keys(void)
{
    char r[24], a[24], b[24], a1[24], b1[24], got[24], made[24];
    const owf_dcell *kids[4];
    const char *why;
    int steps;
    fresh();
    odg_mind_new(&doc, PX(100), PX(200), "Plan", r);
    odg_mind_add_child(&doc, r, "Alpha", 0, a);
    odg_mind_add_child(&doc, r, "Beta", 0, b);
    odg_mind_add_child(&doc, a, "One", 0, a1);
    steps = doc.nundo;

    /* The arrows, all on the right: right goes out, left goes back, up and down along the row. */
    CHECK_N(odg_mind_key_do(&doc, r, ODG_MKEY_RIGHT, got, NULL), OWF_OK);
    CHECK_S(got, a);
    CHECK_N(odg_mind_key_do(&doc, a, ODG_MKEY_RIGHT, got, NULL), OWF_OK);
    CHECK_S(got, a1);
    CHECK_N(odg_mind_key_do(&doc, a1, ODG_MKEY_LEFT, got, NULL), OWF_OK);
    CHECK_S(got, a);
    CHECK_N(odg_mind_key_do(&doc, a, ODG_MKEY_LEFT, got, NULL), OWF_OK);
    CHECK_S(got, r);
    CHECK_N(odg_mind_key_do(&doc, r, ODG_MKEY_LEFT, got, NULL), OWF_OK);   /* nothing left of the root */
    CHECK_S(got, r);
    CHECK_N(odg_mind_key_do(&doc, a, ODG_MKEY_DOWN, got, NULL), OWF_OK);
    CHECK_S(got, b);
    CHECK_N(odg_mind_key_do(&doc, b, ODG_MKEY_DOWN, got, NULL), OWF_OK);   /* the end of the row */
    CHECK_S(got, b);
    CHECK_N(odg_mind_key_do(&doc, b, ODG_MKEY_UP, got, NULL), OWF_OK);
    CHECK_S(got, a);
    CHECK_N(doc.nundo, steps);                            /* stepping changes nothing */

    /* Tab and Return: the new node is selected, one Undo step each. */
    CHECK_N(odg_mind_key_do(&doc, b, ODG_MKEY_CHILD, b1, NULL), OWF_OK);
    CHECK(strcmp(b1, b) != 0 && odg_mind_parent(&doc, b1) == cell(b));
    CHECK_N(doc.nundo, steps + 1);
    CHECK_N(odg_mind_key_do(&doc, a, ODG_MKEY_SIBLING, made, NULL), OWF_OK);
    CHECK_N(odg_mind_children(&doc, r, kids, 4), 3);
    CHECK(kids[0] == cell(a) && kids[1] == cell(made) && kids[2] == cell(b));
    CHECK_N(odg_undo(&doc), OWF_OK);
    CHECK(cell(made) == NULL);
    CHECK_N(odg_mind_key_do(&doc, a, ODG_MKEY_SIBLING_ABOVE, made, NULL), OWF_OK);
    CHECK_N(odg_mind_children(&doc, r, kids, 4), 3);
    CHECK(kids[0] == cell(made) && kids[1] == cell(a));
    CHECK_N(odg_undo(&doc), OWF_OK);
    why = NULL;
    CHECK_N(odg_mind_key_do(&doc, r, ODG_MKEY_SIBLING, got, &why), OWF_ERR_FORMAT);
    CHECK_S(why, "a map's root has no siblings");

    /* . folds and unfolds; - and + fold and unfold; a leaf has nothing to fold. */
    CHECK_N(odg_mind_key_do(&doc, a, ODG_MKEY_FOLD, got, NULL), OWF_OK);
    CHECK_S(got, a);
    CHECK(odg_mind_folded(cell(a)) && !shown(a1));
    CHECK_N(odg_mind_key_do(&doc, a, ODG_MKEY_RIGHT, got, NULL), OWF_OK);   /* not into a folded branch */
    CHECK_S(got, a);
    CHECK_N(odg_mind_key_do(&doc, a, ODG_MKEY_FOLD, got, NULL), OWF_OK);
    CHECK(!odg_mind_folded(cell(a)) && shown(a1));
    CHECK_N(odg_mind_key_do(&doc, a, ODG_MKEY_FOLD_ON, got, NULL), OWF_OK);
    CHECK(odg_mind_folded(cell(a)));
    CHECK_N(odg_mind_key_do(&doc, a, ODG_MKEY_FOLD_OFF, got, NULL), OWF_OK);
    CHECK(!odg_mind_folded(cell(a)));
    CHECK_N(odg_mind_key_do(&doc, a1, ODG_MKEY_FOLD, got, &why), OWF_ERR_FORMAT);
    CHECK_S(why, "it has no children");

    /* Alt-Up and Alt-Down keep the node selected; the end of the row says so. */
    CHECK_N(odg_mind_key_do(&doc, a, ODG_MKEY_MOVE_UP, got, &why), OWF_ERR_FORMAT);
    CHECK_S(why, "it is at the end of its row already");
    CHECK_N(odg_mind_key_do(&doc, a, ODG_MKEY_MOVE_DOWN, got, NULL), OWF_OK);
    CHECK_S(got, a);
    CHECK_N(odg_mind_children(&doc, r, kids, 4), 2);
    CHECK(kids[0] == cell(b) && kids[1] == cell(a));
    CHECK_N(odg_undo(&doc), OWF_OK);

    /* Del: the branch goes, and the next sibling, the one before, or the parent is selected. */
    CHECK_N(odg_mind_key_do(&doc, a, ODG_MKEY_DELETE, got, NULL), OWF_OK);
    CHECK_S(got, b);
    CHECK(cell(a) == NULL && cell(a1) == NULL);
    CHECK_N(odg_undo(&doc), OWF_OK);
    CHECK_N(odg_mind_key_do(&doc, b, ODG_MKEY_DELETE, got, NULL), OWF_OK);
    CHECK_S(got, a);
    CHECK_N(odg_undo(&doc), OWF_OK);
    CHECK_N(odg_mind_key_do(&doc, a1, ODG_MKEY_DELETE, got, NULL), OWF_OK);
    CHECK_S(got, a);
    CHECK_N(odg_undo(&doc), OWF_OK);

    /* Both sides: Alpha right, Beta left; on the left, left goes out and right goes back. */
    CHECK_N(odg_mind_layout(&doc, r, 1), OWF_OK);
    CHECK(cell(b)->geo.x < cell(r)->geo.x && cell(a)->geo.x > cell(r)->geo.x);
    CHECK_N(odg_mind_key_do(&doc, r, ODG_MKEY_LEFT, got, NULL), OWF_OK);
    CHECK_S(got, b);
    CHECK_N(odg_mind_key_do(&doc, r, ODG_MKEY_RIGHT, got, NULL), OWF_OK);
    CHECK_S(got, a);
    CHECK_N(odg_mind_key_do(&doc, b, ODG_MKEY_LEFT, got, NULL), OWF_OK);
    CHECK_S(got, b1);
    CHECK_N(odg_mind_key_do(&doc, b1, ODG_MKEY_RIGHT, got, NULL), OWF_OK);
    CHECK_S(got, b);
    CHECK_N(odg_mind_key_do(&doc, b, ODG_MKEY_RIGHT, got, NULL), OWF_OK);
    CHECK_S(got, r);
    CHECK_N(odg_mind_key_do(&doc, b, ODG_MKEY_CHILD, made, NULL), OWF_OK);   /* the sides are kept */
    CHECK(cell(made)->geo.x < cell(b)->geo.x && cell(a)->geo.x > cell(r)->geo.x);
    CHECK_N(odg_mind_key_do(&doc, r, ODG_MKEY_LAYOUT, got, NULL), OWF_OK);
    CHECK_S(got, r);

    /* Not a node. */
    why = NULL;
    CHECK_N(odg_mind_key_do(&doc, "nope", ODG_MKEY_CHILD, got, &why), OWF_ERR_FORMAT);
    CHECK_S(why, "no map node by that id");
    CHECK_S(got, "");
    CHECK_N(odg_mind_key_do(&doc, NULL, ODG_MKEY_DOWN, got, &why), OWF_ERR_FORMAT);
    done();
}

/* Budgie's #198 notes: a locked node keeps its place through a layout, and a map past ODG_MIND_MAX nodes says so. */
static void test_locked_and_big(void)
{
    char r[24], a[24], b[24], b1[24], id[24];
    int i;
    owf_dpage *p;
    fresh();
    odg_mind_new(&doc, PX(100), PX(200), "Plan", r);
    odg_mind_add_child(&doc, r, "Alpha", 0, a);
    odg_mind_add_child(&doc, r, "Beta", 0, b);
    odg_mind_add_child(&doc, b, "One", 0, b1);
    cell(b)->geo.x = PX(900);
    cell(b)->geo.y = PX(800);
    owf_dcell_set(&cell(b)->style, ODG_MIND_NODE_STYLE "locked=1;");
    CHECK_N(odg_mind_layout(&doc, r, 0), OWF_OK);
    check_at(b, 900, 800);                                /* locked: left where it is */
    CHECK(cell(b1)->geo.x > cell(b)->geo.x);              /* its branch laid out from there */
    CHECK(cell(a)->geo.x > cell(r)->geo.x);
    CHECK(odg_mind_whole(&doc, r));
    done();

    fresh();                                              /* one more node than a walk takes */
    odg_mind_new(&doc, PX(0), PX(0), "Big", r);
    p = odg_page(&doc);
    for (i = 0; i < ODG_MIND_MAX; i++) {
        owf_dcell *c, *e;
        int layer = owf_dpage_index(p, cell(r)->parent);
        snprintf(id, sizeof id, "n%d", i);
        c = owf_dpage_add(p, id, p->cells[layer].id, OWF_DCELL_VERTEX);
        owf_dcell_set(&c->style, ODG_MIND_NODE_STYLE);
        c->geo.width = PX(80);
        c->geo.height = PX(30);
        snprintf(id, sizeof id, "e%d", i);
        e = owf_dpage_add(p, id, p->cells[layer].id, OWF_DCELL_EDGE);
        owf_dcell_set(&e->style, ODG_MIND_EDGE_STYLE);
        owf_dcell_set(&e->source, r);
        snprintf(id, sizeof id, "n%d", i);
        owf_dcell_set(&e->target, id);
    }
    CHECK(!odg_mind_whole(&doc, r));
    CHECK(!odg_mind_whole(&doc, "n3"));
    CHECK_N(odg_mind_layout(&doc, r, 0), OWF_OK);
    CHECK(!odg_mind_whole(&doc, "nope"));
    done();
}

/* Outlines: a map out as items and in again (one Undo step), folds both ways, labels as plain text, and the
 * trees refused. */
static void test_outline(void)
{
    char r[24], a[24], b[24], a1[24], root2[24];
    owf_ditem *it, back[4];
    int n, steps;
    char t0[] = "Root", t1[] = "A & <b>", t2[] = "two\nlines", t3[] = "Leaf";
    fresh();
    odg_mind_new(&doc, PX(100), PX(200), "Plan", r);
    odg_mind_add_child(&doc, r, "Alpha &amp; co", 0, a);
    odg_mind_add_child(&doc, r, "Beta", 0, b);
    odg_mind_add_child(&doc, a, "One<br>Two", 0, a1);
    odg_mind_fold(&doc, a, 1, 0);
    CHECK_N(odg_mind_items(&doc, a1, &it, &n), OWF_OK);   /* from any node: the whole map */
    CHECK_N(n, 4);
    if (n == 4) {
        CHECK_S(it[0].text, "Plan");
        CHECK_N(it[0].depth, 0);
        CHECK_S(it[1].text, "Alpha & co");
        CHECK_N(it[1].depth, 1);
        CHECK(it[1].folded && !it[0].folded);
        CHECK_S(it[2].text, "One\nTwo");                  /* a folded branch is written too */
        CHECK_N(it[2].depth, 2);
        CHECK_S(it[3].text, "Beta");
        CHECK_N(it[3].depth, 1);
    }

    steps = doc.nundo;                                    /* in again, beside it */
    CHECK_N(odg_mind_import(&doc, PX(600), PX(200), it, n, 0, root2), OWF_OK);
    CHECK_N(doc.nundo, steps + 1);
    CHECK(odg_mind_is_node(cell(root2)) && odg_mind_parent(&doc, root2) == NULL);
    check_at(root2, 600, 200);
    owf_outline_free(it, n);
    CHECK_N(odg_mind_items(&doc, root2, &it, &n), OWF_OK);
    CHECK_N(n, 4);
    if (n == 4) {
        const owf_dcell *kids[4];
        CHECK_S(it[1].text, "Alpha & co");
        CHECK(it[1].folded);
        CHECK_S(it[2].text, "One\nTwo");
        CHECK_N(odg_mind_children(&doc, root2, kids, 4), 2);
        CHECK(odg_mind_folded(kids[0]));
        CHECK(!odg_mind_folded(kids[1]));
    }
    owf_outline_free(it, n);
    CHECK_N(odg_undo(&doc), OWF_OK);                      /* one step takes the whole map away */
    CHECK(cell(root2) == NULL);

    back[0].depth = 0, back[0].text = t0, back[0].folded = 0;   /* labels escaped, a folded leaf left open */
    back[1].depth = 1, back[1].text = t1, back[1].folded = 1;
    back[2].depth = 2, back[2].text = t2, back[2].folded = 1;
    back[3].depth = 1, back[3].text = t3, back[3].folded = 0;
    CHECK_N(odg_mind_import(&doc, PX(0), PX(600), back, 4, 1, root2), OWF_OK);
    {
        const owf_dcell *kids[4];
        CHECK_N(odg_mind_children(&doc, root2, kids, 4), 2);
        CHECK_S(kids[0]->value, "A &amp; &lt;b&gt;");
        CHECK(odg_mind_folded(kids[0]));
        CHECK(kids[0]->geo.x > cell(root2)->geo.x && kids[1]->geo.x < cell(root2)->geo.x);   /* both sides */
        CHECK_N(odg_mind_children(&doc, kids[0]->id, kids + 2, 2), 1);
        CHECK_S(kids[2]->value, "two<br>lines");
        CHECK(!odg_mind_folded(kids[2]) && !shown(kids[2]->id));
    }

    steps = doc.nundo;                                    /* not trees: nothing made */
    back[1].depth = 2;
    CHECK_N(odg_mind_import(&doc, 0, 0, back, 4, 0, NULL), OWF_ERR_FORMAT);
    back[1].depth = 0;
    CHECK_N(odg_mind_import(&doc, 0, 0, back, 4, 0, NULL), OWF_ERR_FORMAT);
    back[1].depth = 1;
    back[0].depth = 1;
    CHECK_N(odg_mind_import(&doc, 0, 0, back, 4, 0, NULL), OWF_ERR_FORMAT);
    CHECK_N(odg_mind_import(&doc, 0, 0, back, 0, 0, NULL), OWF_ERR_FORMAT);
    back[0].depth = 0;
    back[1].depth = ODG_MIND_DEPTH;
    CHECK_N(odg_mind_import(&doc, 0, 0, back, 2, 0, NULL), OWF_ERR_FORMAT);
    back[1].depth = 1;
    CHECK_N(odg_layer_lock(&doc, 0, 1), OWF_OK);
    CHECK_N(odg_mind_import(&doc, 0, 0, back, 4, 0, NULL), OWF_ERR_FORMAT);
    CHECK_N(odg_layer_lock(&doc, 0, 0), OWF_OK);
    CHECK_N(doc.nundo, steps + 2);                        /* only the lock and unlock */
    CHECK_N(odg_mind_items(&doc, "nope", &it, &n), OWF_ERR_FORMAT);
    CHECK(it == NULL && n == 0);
    done();
}

int main(void)
{
    test_build_and_layout();
    test_edits();
    test_odd_maps();
    test_keys();
    test_locked_and_big();
    test_outline();
    printf("diagram mind map: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
