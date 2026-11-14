/*
 * GFOpenDiag's editing core on the PC (src/diag/odg_core.c): each operation is one Undo step and fails
 * cleanly; moving a shape moves the ends of the connectors glued to it; ports; orthogonal routing; the
 * grid; hit-testing. Every expected place below was worked out by hand. Run under ASan and UBSan by
 * make check.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <stdio.h>
#include <string.h>
#include "opendiag_core.h"

static int fails, checks;

#define CHECK(cond) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_N(got, want) do { long g_ = (long)(got), w_ = (long)(want); checks++; if (g_ != w_) { fails++; \
    printf("FAIL %s:%d: %s is %ld, want %ld\n", __FILE__, __LINE__, #got, g_, w_); } } while (0)
#define CHECK_S(got, want) do { const char *g_ = (got), *w_ = (want); checks++; if (!g_ || strcmp(g_, w_) != 0) { \
    fails++; printf("FAIL %s:%d: %s is \"%s\", want \"%s\"\n", __FILE__, __LINE__, #got, g_ ? g_ : "(null)", w_); } } while (0)

#define PX(n) OWF_DNUM(n)

static owf_diagram d;
static odg_doc doc;

static void fresh(int grid)
{
    owf_dpage *p;
    owf_diagram_init(&d);
    p = owf_diagram_add_page(&d, "p", "Page-1");
    if (grid) {
        owf_dattrs_set(&p->model, "grid", "1");
        owf_dattrs_set(&p->model, "gridSize", "10");
    }
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

/* The connector's path, end to end. */
static int path(const char *id, owf_dpoint *out, int max)
{
    int n = 0, i;
    owf_dpoint *pts = owf_dedge_path(odg_page(&doc), cell(id), &n);
    for (i = 0; i < n && i < max; i++)
        out[i] = pts[i];
    owf_free(pts);
    return n;
}

static void test_shapes_and_undo(void)
{
    char a[24], b[24];
    fresh(0);
    CHECK(!odg_can_undo(&doc));
    CHECK_N(odg_add_shape(&doc, PX(10), PX(20), PX(80), PX(40), "rounded=1;", "One", a), OWF_OK);
    CHECK_N(odg_add_shape(&doc, PX(200), PX(20), PX(80), PX(40), NULL, "Two", b), OWF_OK);
    CHECK_N(odg_page(&doc)->ncells, 4);
    CHECK_S(cell(a)->parent, "1");
    CHECK_S(cell(a)->value, "One");
    CHECK_N(cell(b)->geo.x, PX(200));
    CHECK_N(doc.nundo, 2);
    CHECK(doc.changed);
    odg_saved(&doc);
    CHECK(!doc.changed);

    /* one step each: undo takes the second shape away, redo puts it back */
    CHECK_N(odg_undo(&doc), OWF_OK);
    CHECK(cell(b) == NULL);
    CHECK(cell(a) != NULL);
    CHECK(doc.changed);
    CHECK_N(odg_redo(&doc), OWF_OK);
    CHECK_S(cell(b)->value, "Two");
    CHECK_N(odg_redo(&doc), OWF_ERR_FORMAT);

    /* a new step after an undo drops what redo had */
    CHECK_N(odg_undo(&doc), OWF_OK);
    CHECK(odg_can_redo(&doc));
    CHECK_N(odg_set_value(&doc, a, "Renamed"), OWF_OK);
    CHECK(!odg_can_redo(&doc));
    CHECK_S(cell(a)->value, "Renamed");
    CHECK_N(odg_undo(&doc), OWF_OK);
    CHECK_S(cell(a)->value, "One");
    CHECK_N(odg_undo(&doc), OWF_OK);
    CHECK_N(odg_page(&doc)->ncells, 2);
    CHECK_N(odg_undo(&doc), OWF_ERR_FORMAT);

    /* failures change nothing and leave no step */
    CHECK_N(odg_add_shape(&doc, 0, 0, 0, PX(10), NULL, NULL, NULL), OWF_ERR_FORMAT);
    CHECK_N(odg_set_value(&doc, "nothing", "x"), OWF_ERR_FORMAT);
    CHECK(!odg_can_undo(&doc));
    done();
}

static void test_undo_limit(void)
{
    int i;
    fresh(0);
    for (i = 0; i < ODG_UNDO_MAX + 20; i++)
        odg_add_shape(&doc, PX(i), 0, PX(5), PX(5), NULL, NULL, NULL);
    CHECK_N(doc.nundo, ODG_UNDO_MAX);
    for (i = 0; i < ODG_UNDO_MAX; i++)
        CHECK_N(odg_undo(&doc), OWF_OK);
    CHECK_N(odg_page(&doc)->ncells, 2 + 20);      /* the oldest 20 steps are kept as done */
    CHECK_N(doc.nredo, ODG_UNDO_MAX);
    done();
}

static void test_glue(void)
{
    char a[24], b[24], e[24], c[24];
    const char *one[1];
    owf_dpoint pts[8];
    int n;
    fresh(0);
    odg_add_shape(&doc, PX(0), PX(0), PX(100), PX(50), NULL, "A", a);
    odg_add_shape(&doc, PX(300), PX(0), PX(100), PX(50), NULL, "B", b);
    CHECK_N(odg_add_edge(&doc, a, b, NULL, NULL, "endArrow=classic;", e), OWF_OK);
    n = path(e, pts, 8);
    CHECK_N(n, 2);
    CHECK_N(pts[0].x, PX(100));                   /* A's right side */
    CHECK_N(pts[0].y, PX(25));
    CHECK_N(pts[1].x, PX(300));                   /* B's left side */

    /* moving B down 200: the glued end follows, and the edge is still one */
    one[0] = b;
    CHECK_N(odg_move(&doc, one, 1, 0, PX(200)), OWF_OK);
    n = path(e, pts, 8);
    CHECK_N(n, 2);
    CHECK_N(pts[1].y, PX(200));                   /* B's top, aimed at from A's middle */
    CHECK_N(pts[0].y, PX(50));                    /* A's bottom, aimed at B's middle */
    CHECK_N(cell(b)->geo.y, PX(200));
    CHECK_N(odg_undo(&doc), OWF_OK);              /* one step */
    CHECK_N(cell(b)->geo.y, 0);

    /* ports: from A's south to B's west */
    CHECK_N(odg_connect(&doc, e, 0, a, ODG_PORT_S, NULL), OWF_OK);
    CHECK_N(odg_connect(&doc, e, 1, b, ODG_PORT_W, NULL), OWF_OK);
    n = path(e, pts, 8);
    CHECK_N(pts[0].x, PX(50));
    CHECK_N(pts[0].y, PX(50));
    CHECK_N(pts[1].x, PX(300));
    CHECK_N(pts[1].y, PX(25));
    {
        char v[16];
        CHECK(owf_dstyle_get(cell(e)->style, "exitY", v, sizeof v));
        CHECK_S(v, "1");
        CHECK(owf_dstyle_get(cell(e)->style, "endArrow", v, sizeof v));
    }
    /* ports stay glued as the shape moves */
    one[0] = a;
    CHECK_N(odg_move(&doc, one, 1, PX(10), PX(10)), OWF_OK);
    n = path(e, pts, 8);
    CHECK_N(pts[0].x, PX(60));
    CHECK_N(pts[0].y, PX(60));

    /* a loose end: unglued to a point, and it stays put when the other end's shape moves */
    CHECK_N(odg_connect(&doc, e, 1, NULL, ODG_PORT_NONE, &pts[1]), OWF_OK);
    CHECK(cell(e)->target == NULL);
    one[0] = a;
    odg_move(&doc, one, 1, PX(5), 0);
    n = path(e, pts, 8);
    CHECK_N(pts[n - 1].x, PX(300));
    CHECK_N(pts[n - 1].y, PX(25));
    CHECK_N(odg_connect(&doc, e, 1, "nothing", ODG_PORT_N, NULL), OWF_ERR_FORMAT);

    /* deleting a shape takes its connectors; undo brings both back */
    odg_add_shape(&doc, PX(0), PX(300), PX(20), PX(20), NULL, NULL, c);
    one[0] = a;
    CHECK_N(odg_delete(&doc, one, 1), OWF_OK);
    CHECK(cell(a) == NULL && cell(e) == NULL);
    CHECK(cell(b) != NULL && cell(c) != NULL);
    CHECK_N(odg_undo(&doc), OWF_OK);
    CHECK(cell(a) != NULL && cell(e) != NULL);
    one[0] = "1";
    CHECK_N(odg_delete(&doc, one, 1), OWF_ERR_FORMAT);   /* not the layer */
    one[0] = "0";
    CHECK_N(odg_delete(&doc, one, 1), OWF_ERR_FORMAT);   /* nor the root */
    one[0] = "nothing";
    CHECK_N(odg_delete(&doc, one, 1), OWF_ERR_FORMAT);
    CHECK_N(odg_add_edge(&doc, a, "nothing", NULL, NULL, NULL, NULL), OWF_ERR_FORMAT);
    done();
}

static void test_moving_both_ends(void)
{
    char a[24], b[24], e[24];
    const char *both[2];
    owf_dcell *edge;
    fresh(0);
    odg_add_shape(&doc, 0, 0, PX(40), PX(40), NULL, NULL, a);
    odg_add_shape(&doc, PX(200), PX(200), PX(40), PX(40), NULL, NULL, b);
    odg_add_edge(&doc, a, b, NULL, NULL, NULL, e);
    edge = cell(e);
    owf_dgeo_add_point(&edge->geo, PX(120), PX(20));
    both[0] = a;
    both[1] = b;
    CHECK_N(odg_move(&doc, both, 2, PX(30), PX(-10)), OWF_OK);
    edge = cell(e);
    CHECK_N(edge->geo.points[0].x, PX(150));      /* both ends moved: the bend goes along */
    CHECK_N(edge->geo.points[0].y, PX(10));
    both[0] = a;
    CHECK_N(odg_move(&doc, both, 1, PX(30), 0), OWF_OK);
    CHECK_N(cell(e)->geo.points[0].x, PX(150));   /* one end: the bend stays */
    done();
}

static void test_orthogonal(void)
{
    char a[24], b[24], c[24], e[24], f[24];
    const char *one[1];
    owf_dcell *edge;
    fresh(0);
    odg_add_shape(&doc, 0, 0, PX(100), PX(40), NULL, NULL, a);           /* middle 50,20 */
    odg_add_shape(&doc, PX(300), PX(100), PX(100), PX(40), NULL, NULL, b); /* middle 350,120 */
    CHECK_N(odg_add_edge(&doc, a, b, NULL, NULL, "edgeStyle=orthogonalEdgeStyle;", e), OWF_OK);
    edge = cell(e);
    /* wider than tall: out of A's east, into B's west, turning at x 200 */
    CHECK_N(edge->geo.npoints, 2);
    CHECK_N(edge->geo.points[0].x, PX(200));
    CHECK_N(edge->geo.points[0].y, PX(20));
    CHECK_N(edge->geo.points[1].x, PX(200));
    CHECK_N(edge->geo.points[1].y, PX(120));
    {
        owf_dpoint pts[8];
        int n = path(e, pts, 8);
        CHECK_N(n, 4);
        CHECK_N(pts[0].x, PX(100));
        CHECK_N(pts[0].y, PX(20));
        CHECK_N(pts[3].x, PX(300));
        CHECK_N(pts[3].y, PX(120));
    }
    /* B moved below A: taller than wide now, so out of A's south, into B's north, turning at y 150 */
    one[0] = b;
    CHECK_N(odg_move(&doc, one, 1, PX(-300), PX(160)), OWF_OK);   /* B at 0,260: middle 50,280 */
    edge = cell(e);
    CHECK_N(edge->geo.npoints, 0);               /* straight down: no turns */
    one[0] = b;
    odg_move(&doc, one, 1, PX(100), 0);           /* middle 150,280 */
    edge = cell(e);
    CHECK_N(edge->geo.npoints, 2);
    CHECK_N(edge->geo.points[0].x, PX(50));
    CHECK_N(edge->geo.points[0].y, PX(150));
    CHECK_N(edge->geo.points[1].x, PX(150));
    CHECK_N(edge->geo.points[1].y, PX(150));
    CHECK_N(odg_undo(&doc), OWF_OK);              /* the move and its routing are one step */
    CHECK_N(cell(e)->geo.npoints, 0);

    /* ports at right angles: one turn */
    odg_add_shape(&doc, PX(400), PX(400), PX(40), PX(40), NULL, NULL, c);   /* middle 420,420 */
    odg_add_edge(&doc, a, c, NULL, NULL, "edgeStyle=orthogonalEdgeStyle;", f);
    edge = cell(f);
    CHECK_N(edge->geo.npoints, 2);                /* taller than wide: down from A, across, down into C */
    CHECK_N(edge->geo.points[0].y, PX(220));
    CHECK_N(odg_connect(&doc, f, 0, a, ODG_PORT_E, NULL), OWF_OK);
    CHECK_N(odg_connect(&doc, f, 1, c, ODG_PORT_N, NULL), OWF_OK);
    edge = cell(f);
    CHECK_N(edge->geo.npoints, 1);
    CHECK_N(edge->geo.points[0].x, PX(420));      /* along A's middle line, then down into C's top */
    CHECK_N(edge->geo.points[0].y, PX(20));
    /* resizing C routes it again */
    CHECK_N(odg_resize(&doc, c, PX(400), PX(400), PX(100), PX(40)), OWF_OK);
    CHECK_N(cell(f)->geo.points[0].x, PX(450));
    CHECK_N(odg_resize(&doc, c, 0, 0, 0, PX(5)), OWF_ERR_FORMAT);
    CHECK_N(odg_route(&doc, a), OWF_ERR_FORMAT);   /* not a connector */
    done();
}

static void test_grid(void)
{
    char a[24], b[24];
    fresh(1);
    CHECK_N(doc.grid, PX(10));
    CHECK_N(odg_snap(&doc, PX(14)), PX(10));
    CHECK_N(odg_snap(&doc, PX(15)), PX(20));
    CHECK_N(odg_snap(&doc, PX(-14)), PX(-10));
    CHECK_N(odg_snap(&doc, PX(-16)), PX(-20));
    CHECK_N(odg_snap(&doc, 449), 0);
    odg_add_shape(&doc, PX(13), PX(27), PX(40), PX(40), NULL, NULL, a);
    CHECK_N(cell(a)->geo.x, PX(10));
    CHECK_N(cell(a)->geo.y, PX(30));
    odg_add_shape(&doc, PX(100), PX(100), PX(40), PX(40), NULL, NULL, b);
    /* moved by 7,4: the first shape's corner snaps (to 20,30), and the others take the same step */
    {
        const char *two[2];
        two[0] = a;
        two[1] = b;
        CHECK_N(odg_move(&doc, two, 2, PX(7), PX(4)), OWF_OK);
    }
    CHECK_N(cell(a)->geo.x, PX(20));
    CHECK_N(cell(a)->geo.y, PX(30));
    CHECK_N(cell(b)->geo.x, PX(110));
    CHECK_N(cell(b)->geo.y, PX(100));
    done();
    fresh(0);
    CHECK_N(doc.grid, 0);
    CHECK_N(odg_snap(&doc, 1234), 1234);
    done();
}

static void test_groups_and_styles(void)
{
    char g[24], e[24], hit[24];
    const char *ids[2];
    owf_dcell *child;
    owf_dpoint pts[4];
    fresh(0);
    odg_add_shape(&doc, PX(100), PX(100), PX(200), PX(200), "swimlane;", "G", g);
    /* a shape inside the group, placed relative to it */
    child = owf_dpage_add(odg_page(&doc), "kid", g, OWF_DCELL_VERTEX);
    child->geo.flags = OWF_DGEO_HAS;
    child->geo.x = PX(20);
    child->geo.y = PX(40);
    child->geo.width = PX(40);
    child->geo.height = PX(20);
    odg_add_edge(&doc, "kid", NULL, NULL, &(owf_dpoint){ PX(500), PX(150) }, NULL, e);
    ids[0] = g;
    CHECK_N(odg_move(&doc, ids, 1, PX(50), 0), OWF_OK);
    CHECK_N(cell("kid")->geo.x, PX(20));          /* its own place doesn't change ... */
    CHECK_N(path(e, pts, 4), 2);
    CHECK_N(pts[0].x, PX(210));                   /* ... but the edge leaves it where it now is */
    CHECK_N(pts[1].x, PX(500));                   /* and the loose end stays */

    ids[0] = g;
    ids[1] = "kid";
    CHECK_N(odg_set_style(&doc, ids, 2, "fillColor", "#ff0000"), OWF_OK);
    CHECK_S(cell("kid")->style, "fillColor=#ff0000;");
    CHECK_S(cell(g)->style, "swimlane;fillColor=#ff0000;");
    CHECK_N(odg_undo(&doc), OWF_OK);
    CHECK(cell("kid")->style == NULL);
    ids[0] = "x";
    CHECK_N(odg_set_style(&doc, ids, 1, "a", "b"), OWF_ERR_FORMAT);

    /* hits: the child above its group, the connector near its line, nothing in empty space */
    CHECK(odg_hit(&doc, PX(180), PX(150), PX(3), hit, sizeof hit));
    CHECK_S(hit, "kid");
    CHECK(odg_hit(&doc, PX(160), PX(250), PX(3), hit, sizeof hit));
    CHECK_S(hit, g);
    CHECK(odg_hit(&doc, PX(400), PX(152), PX(3), hit, sizeof hit));
    CHECK_S(hit, e);
    CHECK(!odg_hit(&doc, PX(400), PX(160), PX(3), hit, sizeof hit));
    CHECK(!odg_hit(&doc, PX(900), PX(900), PX(3), hit, sizeof hit));
    done();
}

static void test_from_file(void)
{
    static const char file[] =
        "<mxfile><diagram id=\"x\" name=\"F\"><mxGraphModel grid=\"1\" gridSize=\"20\"><root>"
        "<mxCell id=\"0\"/><mxCell id=\"1\" parent=\"0\"/>"
        "<mxCell id=\"s\" vertex=\"1\" parent=\"1\"><mxGeometry x=\"0\" y=\"0\" width=\"40\" height=\"40\" as=\"geometry\"/></mxCell>"
        "</root></mxGraphModel></diagram></mxfile>";
    char id[24];
    owf_diagram_init(&d);
    CHECK_N(owf_drawio_read((const unsigned char *)file, sizeof file - 1, &d), OWF_OK);
    odg_init(&doc, &d, 0);
    CHECK_N(doc.grid, PX(20));
    CHECK_N(odg_add_shape(&doc, PX(31), PX(9), PX(10), PX(10), NULL, NULL, id), OWF_OK);
    CHECK_N(cell(id)->geo.x, PX(40));
    CHECK_N(cell(id)->geo.y, 0);
    CHECK(strcmp(id, "s") != 0);
    done();
}

/* Long connectors: the hit test's sums stay inside 64 bits (Budgie's review: an edge 27,000 px long overflowed). */
static void test_long_edges(void)
{
    owf_dpoint a = { PX(0), PX(0) }, b = { PX(27000), PX(27000) }, fa = { -PX(20000000), -PX(20000000) },
               fb = { PX(20000000), PX(20000000) };
    char e[24], hit[24];
    fresh(0);
    CHECK_N(odg_add_edge(&doc, NULL, NULL, &a, &b, "endArrow=none;", e), OWF_OK);
    CHECK(odg_hit(&doc, PX(13500), PX(13500), PX(3), hit, sizeof hit));
    CHECK_S(hit, e);
    CHECK(odg_hit(&doc, PX(13502), PX(13498), PX(3), hit, sizeof hit));      /* 2.8 px off the line */
    CHECK(!odg_hit(&doc, PX(13505), PX(13495), PX(3), hit, sizeof hit));     /* 7.1 px off */
    CHECK(odg_hit(&doc, PX(27002), PX(27000), PX(3), hit, sizeof hit));      /* past the end, near it */
    CHECK(!odg_hit(&doc, PX(27010), PX(27010), PX(3), hit, sizeof hit));
    CHECK(!odg_hit(&doc, -PX(5), -PX(5), PX(3), hit, sizeof hit));
    done();
    fresh(0);
    CHECK_N(odg_add_edge(&doc, NULL, NULL, &fa, &fb, "endArrow=none;", e), OWF_OK);
    CHECK(odg_hit(&doc, 0, 0, PX(3), hit, sizeof hit));
    CHECK(odg_hit(&doc, PX(1000), PX(1000), PX(3), hit, sizeof hit));
    CHECK(!odg_hit(&doc, PX(1000), PX(1010), PX(3), hit, sizeof hit));
    done();
}

/* Ids of any length (Delenn's review): diagrams.net writes 20 characters, "-" and a number, so a big file's
 * cells have ids of 24 characters and more; the core holds cells by index, never by a cut copy of the id. */
static void long_ids(const char *ida, const char *idb, const char *ide)
{
    owf_dpage *p;
    owf_dcell *c;
    const char *ids[1];
    fresh(0);
    p = odg_page(&doc);
    c = owf_dpage_add(p, ida, "1", OWF_DCELL_VERTEX);
    c->geo.flags = OWF_DGEO_HAS;
    c->geo.width = PX(100);
    c->geo.height = PX(50);
    c = owf_dpage_add(p, idb, "1", OWF_DCELL_VERTEX);
    c->geo.flags = OWF_DGEO_HAS;
    c->geo.x = PX(300);
    c->geo.width = PX(100);
    c->geo.height = PX(50);
    c = owf_dpage_add(p, ide, "1", OWF_DCELL_EDGE);
    c->geo.flags = OWF_DGEO_HAS | OWF_DGEO_RELATIVE;
    owf_dcell_set(&c->source, ida);
    owf_dcell_set(&c->target, idb);
    owf_dcell_set(&c->style, "edgeStyle=orthogonalEdgeStyle;");
    CHECK_N(odg_route(&doc, ide), OWF_OK);
    CHECK_N(cell(ide)->geo.npoints, 0);            /* side by side, level: straight across */
    /* joined again at B's west side: its port in the style */
    CHECK_N(odg_connect(&doc, ide, 1, idb, ODG_PORT_W, NULL), OWF_OK);
    CHECK_S(cell(ide)->target, idb);
    CHECK(strstr(cell(ide)->style, "entryX=0;") != NULL);
    /* moving A down routes the connector again: two bends half way across */
    ids[0] = ida;
    CHECK_N(odg_move(&doc, ids, 1, 0, PX(300)), OWF_OK);
    CHECK_N(cell(ide)->geo.npoints, 2);
    if (cell(ide)->geo.npoints == 2) {
        CHECK_N(cell(ide)->geo.points[0].x, PX(200));
        CHECK_N(cell(ide)->geo.points[0].y, PX(325));
        CHECK_N(cell(ide)->geo.points[1].y, PX(25));
    }
    /* resizing B: routed again to its new west side's middle */
    CHECK_N(odg_resize(&doc, idb, PX(300), 0, PX(100), PX(150)), OWF_OK);
    CHECK_N(cell(ide)->geo.points[1].y, PX(75));
    /* and a connector made by the core between them */
    {
        char e2[24];
        CHECK_N(odg_add_edge(&doc, ida, idb, NULL, NULL, "edgeStyle=orthogonalEdgeStyle;", e2), OWF_OK);
        CHECK_N(cell(e2)->geo.npoints, 2);
    }
    CHECK_N(odg_undo(&doc), OWF_OK);
    CHECK_N(odg_undo(&doc), OWF_OK);
    CHECK_N(cell(ide)->geo.points[1].y, PX(25));
    done();
}

static void test_long_ids(void)
{
    long_ids("abcdefghijklmnopqrst-100", "abcdefghijklmnopqrst-101", "abcdefghijklmnopqrst-102");
    long_ids("a-forty-character-id-for-shape-a-0000001", "a-forty-character-id-for-shape-b-0000002",
             "a-forty-character-id-for-connector-00003");
    /* ids that are the same for their first 23 characters: the right one is routed */
    long_ids("abcdefghijklmnopqrstuvw-1", "abcdefghijklmnopqrstuvw-2", "abcdefghijklmnopqrstuvw-3");
}

/* New cells don't go on a hidden or locked layer; a layer is added under a root id of any length. */
static void test_layer_rules(void)
{
    static char longid[200];
    char a[24], b[24], e[24];
    owf_dpage *p;
    int at;
    fresh(0);
    odg_add_shape(&doc, PX(10), PX(10), PX(40), PX(20), NULL, NULL, a);
    CHECK_N(odg_layer_current(&doc), 0);
    CHECK_N(odg_layer_show(&doc, 0, 0), OWF_OK);
    CHECK_N(odg_add_shape(&doc, PX(10), PX(100), PX(40), PX(20), NULL, NULL, b), OWF_ERR_FORMAT);   /* hidden */
    CHECK_N(odg_layer_show(&doc, 0, 1), OWF_OK);
    CHECK_N(odg_layer_lock(&doc, 0, 1), OWF_OK);
    CHECK_N(odg_add_shape(&doc, PX(10), PX(100), PX(40), PX(20), NULL, NULL, b), OWF_ERR_FORMAT);   /* locked */
    CHECK_N(odg_add_edge(&doc, a, a, NULL, NULL, "", e), OWF_ERR_FORMAT);
    CHECK_N(odg_layer_add(&doc, "Top", &at), OWF_OK);                  /* a new layer is open: draw there */
    CHECK_N(odg_layer_current(&doc), 1);
    CHECK_N(odg_add_shape(&doc, PX(10), PX(100), PX(40), PX(20), NULL, NULL, b), OWF_OK);
    CHECK_S(cell(b)->parent, odg_layer_at(&doc, 1)->id);
    done();
    memset(longid, 'r', sizeof longid - 1);                           /* a 199-byte root id from another tool */
    owf_diagram_init(&d);
    p = owf_diagram_add_page(&d, "p", "Page-1");
    owf_dpage_remove(p, "1");                                          /* the page's own root and layer out */
    owf_dpage_remove(p, "0");
    CHECK_N(p->ncells, 0);
    owf_dpage_add(p, longid, NULL, OWF_DCELL_PLAIN);
    owf_dpage_add(p, "L1", longid, OWF_DCELL_PLAIN);
    odg_init(&doc, &d, 0);
    {
        int had = odg_layer_count(&doc);
        CHECK_N(had, 1);
        CHECK_N(odg_layer_add(&doc, NULL, &at), OWF_OK);
        CHECK_N(odg_layer_count(&doc), had + 1);
        CHECK_N(at, had);
        CHECK_S(odg_layer_at(&doc, had)->parent, longid);              /* under the whole id, not a cut one */
    }
    done();
}

static void test_layers(void)
{
    char a[24], b[24], e[24], hit[24];
    int at = -1;
    owf_dpoint p0 = { PX(0), PX(200) }, p1 = { PX(100), PX(200) };
    fresh(0);
    CHECK_N(odg_layer_count(&doc), 1);
    CHECK_S(odg_layer_at(&doc, 0)->id, "1");
    CHECK(odg_layer_at(&doc, 1) == NULL);
    CHECK(odg_layer_at(&doc, -1) == NULL);
    CHECK(odg_layer_shown(&doc, 0));
    CHECK(!odg_layer_locked(&doc, 0));
    odg_add_shape(&doc, PX(10), PX(10), PX(40), PX(20), NULL, NULL, a);

    /* a new layer on top: the current one, so what is drawn next goes on it */
    CHECK_N(odg_layer_add(&doc, NULL, &at), OWF_OK);
    CHECK_N(at, 1);
    CHECK_N(doc.layer, 1);
    CHECK_N(odg_layer_count(&doc), 2);
    CHECK_S(odg_layer_at(&doc, 1)->value, "Layer 2");
    CHECK_S(odg_layer_at(&doc, 1)->parent, "0");
    odg_add_shape(&doc, PX(10), PX(10), PX(40), PX(20), NULL, NULL, b);
    CHECK_S(cell(b)->parent, odg_layer_at(&doc, 1)->id);
    CHECK_S(cell(a)->parent, "1");
    odg_add_edge(&doc, NULL, NULL, &p0, &p1, NULL, e);
    CHECK_S(cell(e)->parent, odg_layer_at(&doc, 1)->id);
    CHECK(odg_hit(&doc, PX(20), PX(20), 0, hit, sizeof hit));
    CHECK_S(hit, b);                              /* the top layer's shape is on top */

    /* hidden: neither drawn nor hit; the layer below shows through */
    CHECK_N(odg_layer_show(&doc, 1, 0), OWF_OK);
    CHECK(!odg_layer_shown(&doc, 1));
    CHECK(!owf_dcell_shown(odg_page(&doc), cell(b)));
    CHECK(odg_hit(&doc, PX(20), PX(20), 0, hit, sizeof hit));
    CHECK_S(hit, a);
    CHECK(!odg_hit(&doc, PX(50), PX(200), PX(2), hit, sizeof hit));
    CHECK_N(odg_layer_show(&doc, 1, 1), OWF_OK);
    CHECK(odg_layer_shown(&doc, 1));
    CHECK(owf_dattrs_get(&odg_layer_at(&doc, 1)->extra, "visible") == NULL);

    /* locked: kept as draw.io writes it, and what is on it counts as locked */
    CHECK_N(odg_layer_lock(&doc, 0, 1), OWF_OK);
    CHECK(odg_layer_locked(&doc, 0));
    CHECK_S(odg_layer_at(&doc, 0)->style, "locked=1;");
    CHECK(odg_cell_locked(&doc, cell(a)));
    CHECK(!odg_cell_locked(&doc, cell(b)));
    CHECK_N(odg_layer_lock(&doc, 0, 0), OWF_OK);
    CHECK(!odg_cell_locked(&doc, cell(a)));
    odg_set_style(&doc, (const char *[]){ b }, 1, "locked", "1");     /* a cell locked itself */
    CHECK(odg_cell_locked(&doc, cell(b)));

    CHECK_N(odg_layer_rename(&doc, 1, "Notes"), OWF_OK);
    CHECK_S(odg_layer_at(&doc, 1)->value, "Notes");
    CHECK_N(odg_layer_rename(&doc, 2, "x"), OWF_ERR_FORMAT);
    CHECK_N(odg_layer_show(&doc, 5, 0), OWF_ERR_FORMAT);
    CHECK_N(odg_layer_lock(&doc, -1, 1), OWF_ERR_FORMAT);
    CHECK_N(odg_layer_rename(&doc, 0, NULL), OWF_ERR_FORMAT);

    /* each is one Undo step; undone past the new layer, new cells go on the first again */
    CHECK_N(odg_undo(&doc), OWF_OK);
    CHECK_S(odg_layer_at(&doc, 1)->value, "Layer 2");
    while (odg_layer_count(&doc) > 1 && odg_can_undo(&doc))
        odg_undo(&doc);
    CHECK_N(odg_layer_count(&doc), 1);
    CHECK_N(doc.layer, 1);                        /* out of range now: the first is used */
    CHECK_N(odg_add_shape(&doc, PX(0), PX(0), PX(5), PX(5), NULL, NULL, b), OWF_OK);
    CHECK_S(cell(b)->parent, "1");
    CHECK_N(odg_layer_add(&doc, "Top", &at), OWF_OK);
    CHECK_S(odg_layer_at(&doc, 1)->value, "Top");
    done();
}

int main(void)
{
    test_shapes_and_undo();
    test_undo_limit();
    test_glue();
    test_moving_both_ends();
    test_orthogonal();
    test_grid();
    test_groups_and_styles();
    test_from_file();
    test_long_edges();
    test_long_ids();
    test_layers();
    test_layer_rules();
    printf("diagram core: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
