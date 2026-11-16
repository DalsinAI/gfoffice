/*
 * GFOpenDiag's clipboard on the PC (src/diag/odg_clip.c): Copy, Cut, Paste and Duplicate keep connectors, give
 * new ids, land where they should, and are one Undo step each. Every expected place below was worked out by hand.
 * Run under ASan and UBSan by make check.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <stdio.h>
#include <string.h>
#include "opendiag_clip.h"

static int fails, checks;

#define CHECK(cond) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_N(got, want) do { long g_ = (long)(got), w_ = (long)(want); checks++; if (g_ != w_) { fails++; \
    printf("FAIL %s:%d: %s is %ld, want %ld\n", __FILE__, __LINE__, #got, g_, w_); } } while (0)
#define CHECK_S(got, want) do { const char *g_ = (got), *w_ = (want); checks++; if (!g_ || strcmp(g_, w_) != 0) { \
    fails++; printf("FAIL %s:%d: %s is \"%s\", want \"%s\"\n", __FILE__, __LINE__, #got, g_ ? g_ : "(null)", w_); } } while (0)

#define PX(n) OWF_DNUM(n)

static owf_diagram d;
static odg_doc doc;
static odg_clip clip;
static char a[24], b[24], e[24];

static void fresh(void)
{
    owf_diagram_init(&d);
    owf_diagram_add_page(&d, "p", "Page-1");
    odg_init(&doc, &d, 0);
    odg_clip_init(&clip);
}

/* Shapes a at 10,20 and b at 200,20, both 80 by 40, and connector e from a to b. */
static void two_joined(const char *edge_style)
{
    fresh();
    odg_add_shape(&doc, PX(10), PX(20), PX(80), PX(40), "rounded=1;", "One", a);
    odg_add_shape(&doc, PX(200), PX(20), PX(80), PX(40), NULL, "Two", b);
    odg_add_edge(&doc, a, b, NULL, NULL, edge_style, e);
}

static void done(void)
{
    odg_clip_free(&clip);
    odg_free(&doc);
    owf_diagram_free(&d);
}

static owf_dcell *cell(const char *id)
{
    return owf_dpage_find(odg_page(&doc), id);
}

static int ncells(void)
{
    return odg_page(&doc)->ncells;
}

static void test_add_copy(void)
{
    owf_dcell *c;
    two_joined(NULL);
    CHECK(owf_dpage_add_copy(&clip.cells, cell(a), a) != NULL);
    CHECK(owf_dpage_add_copy(&clip.cells, cell(b), a) == NULL);          /* that id is taken */
    c = owf_dpage_add_copy(&clip.cells, cell(e), NULL);
    CHECK(c != NULL);
    if (c) {
        CHECK(strcmp(c->id, a) != 0);           /* a new id */
        CHECK_S(c->source, a);
        CHECK_S(c->target, b);
    }
    CHECK_S(clip.cells.cells[0].value, "One");
    CHECK_S(clip.cells.cells[0].style, "rounded=1;");
    CHECK_N(clip.cells.cells[0].geo.width, PX(80));
    done();
}

static void test_copy_paste_keeps_connector(void)
{
    char made[4][ODG_CLIP_ID];
    const char *both[2];
    int n = -1, before;
    owf_dcell *na, *nb, *ne;
    two_joined(NULL);
    both[0] = a;
    both[1] = b;
    before = doc.nundo;
    CHECK_N(odg_copy(&doc, both, 2, &clip), OWF_OK);
    CHECK_N(doc.nundo, before);                 /* Copy is no Undo step */
    CHECK_N(clip.cells.ncells, 3);              /* the connector between them came along */
    CHECK(!odg_clip_empty(&clip));
    CHECK_N(odg_paste_next(&doc, &clip, made, 4, &n), OWF_OK);
    CHECK_N(doc.nundo, before + 1);
    CHECK_N(n, 3);
    CHECK_N(ncells(), 2 + 6);
    na = cell(made[0]);
    nb = cell(made[1]);
    ne = cell(made[2]);
    CHECK(na && nb && ne);
    if (na && nb && ne) {
        CHECK(strcmp(made[0], a) && strcmp(made[1], b) && strcmp(made[2], e));
        CHECK_N(na->geo.x, PX(20));             /* the first Paste is 10 down and right */
        CHECK_N(na->geo.y, PX(30));
        CHECK_N(nb->geo.x, PX(210));
        CHECK_S(na->value, "One");
        CHECK_S(na->parent, "1");
        CHECK_S(ne->parent, "1");
        CHECK_S(ne->source, made[0]);           /* joined to the copies, not the first shapes */
        CHECK_S(ne->target, made[1]);
    }
    CHECK_N(odg_paste_next(&doc, &clip, made, 4, &n), OWF_OK);
    na = cell(made[0]);
    CHECK(na != NULL);
    if (na)
        CHECK_N(na->geo.x, PX(30));             /* the second 10 further */
    CHECK_N(ncells(), 2 + 9);
    CHECK_N(odg_undo(&doc), OWF_OK);
    CHECK_N(ncells(), 2 + 6);
    CHECK_N(odg_undo(&doc), OWF_OK);
    CHECK_N(ncells(), 2 + 3);
    done();
}

static void test_loose_end(void)
{
    char made[4][ODG_CLIP_ID];
    const char *two[2];
    int n = 0;
    owf_dcell *ne;
    two_joined("exitX=1;exitY=0.5;entryX=0;entryY=0.5;");
    two[0] = a;
    CHECK_N(odg_copy(&doc, two, 1, &clip), OWF_OK);
    CHECK_N(clip.cells.ncells, 1);              /* a connector to a shape left behind isn't taken unasked */
    two[1] = e;
    CHECK_N(odg_copy(&doc, two, 2, &clip), OWF_OK);
    CHECK_N(clip.cells.ncells, 2);
    CHECK_N(odg_paste(&doc, &clip, PX(0), PX(100), made, 4, &n), OWF_OK);
    CHECK_N(n, 2);
    ne = cell(made[1]);
    CHECK(ne != NULL);
    if (ne) {
        CHECK_S(ne->source, made[0]);
        CHECK(ne->target == NULL);              /* loose where b's side was, moved with the paste */
        CHECK(ne->geo.flags & OWF_DGEO_TARGET_POINT);
        CHECK_N(ne->geo.target_point.x, PX(200));
        CHECK_N(ne->geo.target_point.y, PX(140));
        CHECK(!strstr(ne->style, "entryX") && !strstr(ne->style, "entryY"));
        CHECK(strstr(ne->style, "exitX=1") != NULL);
    }
    CHECK_N(cell(e)->geo.flags & OWF_DGEO_TARGET_POINT, 0);   /* the first one is as it was */
    CHECK_S(cell(e)->target, b);
    done();
}

static void test_cut(void)
{
    char made[4][ODG_CLIP_ID];
    const char *one[1];
    int n = 0, before;
    two_joined(NULL);
    one[0] = a;
    before = doc.nundo;
    CHECK_N(odg_cut(&doc, one, 1, &clip), OWF_OK);
    CHECK_N(doc.nundo, before + 1);
    CHECK(cell(a) == NULL);
    CHECK(cell(e) == NULL);                     /* its connector went too */
    CHECK(cell(b) != NULL);
    CHECK_N(odg_paste_next(&doc, &clip, made, 4, &n), OWF_OK);
    CHECK_N(n, 1);
    CHECK_N(cell(made[0])->geo.x, PX(10));      /* the first Paste after a Cut lands where it was */
    CHECK_N(cell(made[0])->geo.y, PX(20));
    CHECK_N(odg_paste_next(&doc, &clip, made, 4, &n), OWF_OK);
    CHECK_N(cell(made[0])->geo.x, PX(20));
    CHECK_N(odg_undo(&doc), OWF_OK);
    CHECK_N(odg_undo(&doc), OWF_OK);
    CHECK_N(odg_undo(&doc), OWF_OK);            /* the Cut undone: both shapes and the connector back */
    CHECK(cell(a) && cell(b) && cell(e));
    done();
}

static void test_groups(void)
{
    char made[4][ODG_CLIP_ID];
    const char *one[1];
    owf_dpage *p;
    owf_dcell *g, *kid;
    int n = 0;
    fresh();
    p = odg_page(&doc);
    g = owf_dpage_add(p, "g", "1", OWF_DCELL_VERTEX);
    g->geo.flags = OWF_DGEO_HAS;
    g->geo.x = PX(100);
    g->geo.y = PX(100);
    g->geo.width = PX(200);
    g->geo.height = PX(100);
    owf_dcell_set(&g->style, "group;");
    kid = owf_dpage_add(p, "k", "g", OWF_DCELL_VERTEX);
    kid->geo.flags = OWF_DGEO_HAS;
    kid->geo.x = PX(10);
    kid->geo.y = PX(20);
    kid->geo.width = PX(30);
    kid->geo.height = PX(30);

    one[0] = "g";                               /* the group: what is inside comes too, still inside */
    CHECK_N(odg_copy(&doc, one, 1, &clip), OWF_OK);
    CHECK_N(clip.cells.ncells, 2);
    CHECK_N(odg_paste(&doc, &clip, PX(0), PX(200), made, 4, &n), OWF_OK);
    CHECK_N(n, 1);
    p = odg_page(&doc);
    kid = &p->cells[p->ncells - 1];
    CHECK_S(kid->parent, made[0]);
    CHECK_N(kid->geo.x, PX(10));
    CHECK_N(cell(made[0])->geo.y, PX(300));

    one[0] = "k";                               /* one shape out of the group: it comes out where it is */
    CHECK_N(odg_copy(&doc, one, 1, &clip), OWF_OK);
    CHECK_N(clip.cells.ncells, 1);
    CHECK_N(odg_paste(&doc, &clip, PX(0), PX(0), made, 4, &n), OWF_OK);
    CHECK_S(cell(made[0])->parent, "1");
    CHECK_N(cell(made[0])->geo.x, PX(110));
    CHECK_N(cell(made[0])->geo.y, PX(120));
    done();
}

static void test_labels_and_bends(void)
{
    char made[4][ODG_CLIP_ID];
    const char *ids[3];
    owf_dpage *p;
    owf_dcell *lab, *ne;
    int n = 0;
    two_joined("edgeStyle=orthogonalEdgeStyle;");
    p = odg_page(&doc);
    lab = owf_dpage_add(p, "lab", e, OWF_DCELL_VERTEX);
    lab->geo.flags = OWF_DGEO_HAS | OWF_DGEO_RELATIVE;
    owf_dcell_set(&lab->value, "yes");
    owf_dgeo_add_point(&cell(e)->geo, PX(150), PX(40));

    ids[0] = "lab";                             /* a connector's label alone: nothing to copy */
    CHECK_N(odg_copy(&doc, ids, 1, &clip), OWF_ERR_FORMAT);
    CHECK(odg_clip_empty(&clip));
    ids[0] = a;
    ids[1] = b;
    CHECK_N(odg_copy(&doc, ids, 2, &clip), OWF_OK);
    CHECK_N(clip.cells.ncells, 4);              /* both shapes, the connector and its label */
    CHECK_N(odg_paste(&doc, &clip, PX(5), PX(7), made, 4, &n), OWF_OK);
    p = odg_page(&doc);
    ne = cell(made[2]);
    CHECK(ne != NULL);
    if (ne) {
        CHECK(ne->geo.npoints >= 1);
        if (ne->geo.npoints >= 1) {
            CHECK_N(ne->geo.points[ne->geo.npoints - 1].x, cell(e)->geo.points[cell(e)->geo.npoints - 1].x + PX(5));
            CHECK_N(ne->geo.points[ne->geo.npoints - 1].y, cell(e)->geo.points[cell(e)->geo.npoints - 1].y + PX(7));
        }
    }
    lab = &p->cells[p->ncells - 1];
    CHECK_S(lab->parent, made[2]);
    CHECK_S(lab->value, "yes");
    done();
}

static void test_duplicate(void)
{
    char made[2][ODG_CLIP_ID];
    const char *ids[3];
    int n = 0, before;
    two_joined(NULL);
    ids[0] = b;
    CHECK_N(odg_copy(&doc, ids, 1, &clip), OWF_OK);
    ids[0] = a;
    ids[1] = b;
    ids[2] = e;
    before = doc.nundo;
    CHECK_N(odg_duplicate(&doc, ids, 3, PX(0), PX(60), made, 2, &n), OWF_OK);
    CHECK_N(doc.nundo, before + 1);
    CHECK_N(n, 3);                              /* three made, two of them given back */
    CHECK_N(ncells(), 2 + 6);
    CHECK_N(cell(made[1])->geo.y, PX(80));
    CHECK_N(clip.cells.ncells, 1);              /* the clipboard is as it was */
    CHECK_S(clip.cells.cells[0].id, b);
    CHECK_N(odg_undo(&doc), OWF_OK);
    CHECK_N(ncells(), 2 + 3);
    done();
}

static void test_refused(void)
{
    char made[2][ODG_CLIP_ID];
    const char *ids[2];
    int n = 7, before, at = -1;
    two_joined(NULL);
    CHECK_N(odg_paste_next(&doc, &clip, made, 2, &n), OWF_ERR_FORMAT);    /* nothing on the clipboard */
    CHECK_N(n, 0);
    ids[0] = "1";
    ids[1] = "nope";
    CHECK_N(odg_copy(&doc, ids, 2, &clip), OWF_ERR_FORMAT);               /* a layer and no such cell */
    CHECK_N(odg_cut(&doc, ids, 2, &clip), OWF_ERR_FORMAT);
    CHECK_N(odg_copy(&doc, ids, 0, &clip), OWF_ERR_FORMAT);
    CHECK(cell("1") != NULL);
    ids[0] = a;
    CHECK_N(odg_copy(&doc, ids, 1, &clip), OWF_OK);
    CHECK_N(odg_copy(&doc, ids + 1, 1, &clip), OWF_ERR_FORMAT);           /* a failed Copy keeps what was there */
    CHECK_N(clip.cells.ncells, 1);
    odg_layer_add(&doc, "Top", &at);
    odg_layer_lock(&doc, at, 1);
    before = doc.nundo;
    CHECK_N(odg_paste_next(&doc, &clip, made, 2, &n), OWF_ERR_FORMAT);    /* the current layer is locked */
    CHECK_N(doc.nundo, before);
    odg_layer_lock(&doc, at, 0);
    CHECK_N(odg_paste_next(&doc, &clip, made, 2, &n), OWF_OK);
    CHECK_S(cell(made[0])->parent, odg_layer_at(&doc, at)->id);           /* on the current layer */
    done();
}

static void test_other_page(void)
{
    char made[2][ODG_CLIP_ID];
    const char *ids[1];
    owf_diagram d2;
    odg_doc doc2;
    int n = 0;
    two_joined(NULL);
    ids[0] = a;
    CHECK_N(odg_copy(&doc, ids, 1, &clip), OWF_OK);
    owf_diagram_init(&d2);
    owf_diagram_add_page(&d2, "q", "Other");
    odg_init(&doc2, &d2, 0);
    CHECK_N(odg_paste(&doc2, &clip, PX(0), PX(0), made, 2, &n), OWF_OK);  /* into another diagram */
    CHECK_N(n, 1);
    CHECK_S(owf_dpage_find(odg_page(&doc2), made[0])->value, "One");
    CHECK_N(owf_dpage_find(odg_page(&doc2), made[0])->geo.x, PX(10));
    odg_free(&doc2);
    owf_diagram_free(&d2);
    done();
}

int main(void)
{
    test_add_copy();
    test_copy_paste_keeps_connector();
    test_loose_end();
    test_cut();
    test_groups();
    test_labels_and_bends();
    test_duplicate();
    test_refused();
    test_other_page();
    printf("diagram clipboard: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
