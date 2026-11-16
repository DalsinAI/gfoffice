/*
 * GFOpenDiag's drawing order and Group and Ungroup on the PC (src/diag/odg_order.c): each is one Undo step, cells
 * keep what is inside them, a group's box is round its cells and their places stay where they were on the page.
 * Every expected order and place below was worked out by hand. Run under ASan and UBSan by make check.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <stdio.h>
#include <string.h>
#include "opendiag_order.h"

static int fails, checks;

#define CHECK(cond) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_N(got, want) do { long g_ = (long)(got), w_ = (long)(want); checks++; if (g_ != w_) { fails++; \
    printf("FAIL %s:%d: %s is %ld, want %ld\n", __FILE__, __LINE__, #got, g_, w_); } } while (0)
#define CHECK_S(got, want) do { const char *g_ = (got), *w_ = (want); checks++; if (!g_ || strcmp(g_, w_) != 0) { \
    fails++; printf("FAIL %s:%d: %s is \"%s\", want \"%s\"\n", __FILE__, __LINE__, #got, g_ ? g_ : "(null)", w_); } } while (0)

#define PX(n) OWF_DNUM(n)

static owf_diagram d;
static odg_doc doc;

static owf_dcell *put(const char *id, const char *parent, long x, long y, long w, long h)
{
    owf_dcell *c = owf_dpage_add(odg_page(&doc), id, parent, OWF_DCELL_VERTEX);
    c->geo.flags = OWF_DGEO_HAS;
    c->geo.x = PX(x);
    c->geo.y = PX(y);
    c->geo.width = PX(w);
    c->geo.height = PX(h);
    return c;
}

/* Shapes A, B, C, D on layer 1 in that order; A holds a1 and a2. */
static void fresh(void)
{
    owf_diagram_init(&d);
    owf_diagram_add_page(&d, "p", "Page-1");
    odg_init(&doc, &d, 0);
    put("A", "1", 0, 0, 100, 100);
    put("a1", "A", 10, 10, 20, 20);
    put("a2", "A", 40, 10, 20, 20);
    put("B", "1", 200, 0, 50, 50);
    put("C", "1", 300, 0, 50, 50);
    put("D", "1", 400, 0, 50, 50);
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

/* The page's ids after the root and the layer, joined by spaces. */
static const char *order(void)
{
    static char buf[256];
    const owf_dpage *p = odg_page(&doc);
    int i;
    buf[0] = 0;
    for (i = 2; i < p->ncells; i++) {
        if (buf[0])
            strcat(buf, " ");
        strncat(buf, p->cells[i].id, sizeof buf - strlen(buf) - 2);
    }
    return buf;
}

static void test_front_and_back(void)
{
    const char *ids[2];
    int before;
    fresh();
    ids[0] = "A";
    before = doc.nundo;
    CHECK_N(odg_reorder(&doc, ids, 1, ODG_ORDER_FRONT), OWF_OK);
    CHECK_S(order(), "B C D A a1 a2");          /* with what is inside it */
    CHECK_N(doc.nundo, before + 1);
    CHECK_N(odg_reorder(&doc, ids, 1, ODG_ORDER_FRONT), OWF_ERR_FORMAT);   /* already at the front: no step */
    CHECK_N(doc.nundo, before + 1);
    CHECK_N(odg_reorder(&doc, ids, 1, ODG_ORDER_BACK), OWF_OK);
    CHECK_S(order(), "A a1 a2 B C D");
    CHECK_N(odg_undo(&doc), OWF_OK);
    CHECK_S(order(), "B C D A a1 a2");
    CHECK_N(odg_undo(&doc), OWF_OK);
    CHECK_S(order(), "A a1 a2 B C D");

    ids[0] = "B";                               /* two at once keep their order */
    ids[1] = "C";
    CHECK_N(odg_reorder(&doc, ids, 2, ODG_ORDER_FRONT), OWF_OK);
    CHECK_S(order(), "A a1 a2 D B C");
    CHECK_N(odg_reorder(&doc, ids, 2, ODG_ORDER_BACK), OWF_OK);
    CHECK_S(order(), "B C A a1 a2 D");
    done();
}

static void test_one_step(void)
{
    const char *ids[2];
    fresh();
    ids[0] = "B";
    CHECK_N(odg_reorder(&doc, ids, 1, ODG_ORDER_FORWARD), OWF_OK);
    CHECK_S(order(), "A a1 a2 C B D");
    CHECK_N(odg_reorder(&doc, ids, 1, ODG_ORDER_BACKWARD), OWF_OK);
    CHECK_S(order(), "A a1 a2 B C D");
    CHECK_N(odg_reorder(&doc, ids, 1, ODG_ORDER_BACKWARD), OWF_OK);
    CHECK_S(order(), "B A a1 a2 C D");          /* under A and all of what is inside it */
    CHECK_N(odg_reorder(&doc, ids, 1, ODG_ORDER_BACKWARD), OWF_ERR_FORMAT);
    ids[0] = "A";
    CHECK_N(odg_reorder(&doc, ids, 1, ODG_ORDER_FORWARD), OWF_OK);
    CHECK_S(order(), "B C A a1 a2 D");

    ids[0] = "C";                               /* two side by side go up together */
    ids[1] = "A";
    CHECK_N(odg_reorder(&doc, ids, 2, ODG_ORDER_FORWARD), OWF_OK);
    CHECK_S(order(), "B D C A a1 a2");
    CHECK_N(odg_reorder(&doc, ids, 2, ODG_ORDER_BACKWARD), OWF_OK);
    CHECK_S(order(), "B C A a1 a2 D");

    ids[0] = "a1";                              /* inside a shape: among the cells beside it only */
    CHECK_N(odg_reorder(&doc, ids, 1, ODG_ORDER_FRONT), OWF_OK);
    CHECK_S(order(), "B C A a2 a1 D");
    CHECK_N(odg_reorder(&doc, ids, 1, ODG_ORDER_FORWARD), OWF_ERR_FORMAT);
    done();
}

static void test_reorder_refused(void)
{
    const char *ids[2];
    fresh();
    ids[0] = "1";
    ids[1] = "nope";
    CHECK_N(odg_reorder(&doc, ids, 2, ODG_ORDER_FRONT), OWF_ERR_FORMAT);
    CHECK_N(odg_reorder(&doc, ids, 0, ODG_ORDER_FRONT), OWF_ERR_FORMAT);
    ids[0] = "B";
    CHECK_N(odg_reorder(&doc, ids, 1, (odg_order)9), OWF_ERR_FORMAT);
    ids[1] = "A";                               /* A and B are already the bottom two */
    CHECK_N(odg_reorder(&doc, ids, 2, ODG_ORDER_BACK), OWF_ERR_FORMAT);   /* already at the back */
    CHECK_N(doc.nundo, 0);
    CHECK_N(odg_reorder(&doc, ids, 2, ODG_ORDER_FRONT), OWF_OK);
    CHECK_S(order(), "C D A a1 a2 B");
    CHECK_N(doc.nundo, 1);
    done();
}

static void test_group(void)
{
    char g[ODG_ORDER_ID], made[4][ODG_ORDER_ID];
    const char *ids[3];
    owf_dcell *gc;
    owf_dnum box[4];
    int n = 0, before;
    fresh();
    ids[0] = "C";
    ids[1] = "B";
    ids[2] = "a1";                              /* not beside B and C: passed over */
    before = doc.nundo;
    CHECK_N(odg_group(&doc, ids, 3, g), OWF_OK);
    CHECK_N(doc.nundo, before + 1);
    gc = cell(g);
    CHECK(gc != NULL);
    if (!gc) {
        done();
        return;
    }
    CHECK(odg_is_group(gc));
    CHECK_S(owf_dattrs_get(&gc->extra, "connectable"), "0");
    CHECK_S(gc->parent, "1");
    CHECK_N(gc->geo.x, PX(200));                /* round B and C: 200,0 to 350,50 */
    CHECK_N(gc->geo.y, PX(0));
    CHECK_N(gc->geo.width, PX(150));
    CHECK_N(gc->geo.height, PX(50));
    CHECK_S(cell("B")->parent, g);
    CHECK_S(cell("C")->parent, g);
    CHECK_S(cell("a1")->parent, "A");
    CHECK_N(cell("C")->geo.x, PX(100));         /* from the group's corner */
    CHECK(owf_dcell_box(odg_page(&doc), cell("C"), box));
    CHECK_N(box[0], PX(300));                   /* and still where it was on the page */
    {
        char want[64];
        snprintf(want, sizeof want, "A a1 a2 %s B C D", g);
        CHECK_S(order(), want);                 /* the group where B was */
    }

    ids[0] = g;                                 /* Ungroup gives B and C back, as they were */
    CHECK_N(odg_ungroup(&doc, ids, 1, made, 4, &n), OWF_OK);
    CHECK_N(n, 2);
    CHECK_S(made[0], "B");
    CHECK_S(made[1], "C");
    CHECK(cell(g) == NULL);
    CHECK_S(order(), "A a1 a2 B C D");
    CHECK_S(cell("C")->parent, "1");
    CHECK_N(cell("C")->geo.x, PX(300));
    CHECK_N(odg_undo(&doc), OWF_OK);
    CHECK(cell(g) != NULL);
    CHECK_N(odg_undo(&doc), OWF_OK);
    CHECK(cell(g) == NULL);
    CHECK_S(cell("B")->parent, "1");
    done();
}

static void test_group_in_group_and_connectors(void)
{
    char g1[ODG_ORDER_ID], g2[ODG_ORDER_ID], e[24], made[4][ODG_ORDER_ID];
    const char *ids[3];
    owf_dnum box[4];
    owf_dcell *ec;
    int n = 0;
    fresh();
    odg_add_edge(&doc, "B", "D", NULL, NULL, NULL, e);
    ec = cell(e);
    owf_dgeo_add_point(&ec->geo, PX(325), PX(100));
    ids[0] = "B";
    ids[1] = "C";
    CHECK_N(odg_group(&doc, ids, 2, g1), OWF_OK);
    ids[0] = g1;
    ids[1] = "D";
    ids[2] = e;                                 /* the connector and its bend go in too */
    CHECK_N(odg_group(&doc, ids, 3, g2), OWF_OK);
    CHECK_S(cell(g1)->parent, g2);
    CHECK_S(cell(e)->parent, g2);
    CHECK_N(cell(g2)->geo.x, PX(200));
    CHECK_N(cell(g2)->geo.height, PX(100));     /* down to the bend at 100 */
    CHECK_N(cell(e)->geo.points[0].x, PX(125));
    CHECK(owf_dcell_box(odg_page(&doc), cell("C"), box));
    CHECK_N(box[0], PX(300));                   /* through both groups */
    CHECK_N(cell("C")->geo.x, PX(100));
    CHECK_N(cell(g1)->geo.x, PX(0));

    ids[0] = g2;
    CHECK_N(odg_ungroup(&doc, ids, 1, made, 4, &n), OWF_OK);
    CHECK_N(n, 3);
    CHECK_S(cell(g1)->parent, "1");
    CHECK_N(cell(g1)->geo.x, PX(200));
    CHECK_N(cell(e)->geo.points[0].x, PX(325));
    CHECK(cell(e)->source && !strcmp(cell(e)->source, "B"));   /* still joined */
    CHECK(owf_dcell_box(odg_page(&doc), cell("C"), box));
    CHECK_N(box[0], PX(300));
    done();
}

static void test_ungroup_container(void)
{
    char made[4][ODG_ORDER_ID];
    const char *ids[2];
    int n = 0;
    fresh();
    ids[0] = "A";                               /* A is a shape that holds others, not a group: it stays */
    CHECK_N(odg_ungroup(&doc, ids, 1, made, 1, &n), OWF_OK);
    CHECK_N(n, 2);                              /* two came out, one id given back */
    CHECK_S(made[0], "a1");
    CHECK(cell("A") != NULL);
    CHECK_S(cell("a2")->parent, "1");
    CHECK_N(cell("a2")->geo.x, PX(40));
    CHECK_S(order(), "A a1 a2 B C D");          /* just over it */
    CHECK_N(odg_ungroup(&doc, ids, 1, made, 4, &n), OWF_ERR_FORMAT);   /* empty now */
    ids[0] = "B";
    ids[1] = "1";
    CHECK_N(odg_ungroup(&doc, ids, 2, made, 4, &n), OWF_ERR_FORMAT);
    CHECK_N(n, 0);
    CHECK_N(odg_group(&doc, ids + 1, 1, NULL), OWF_ERR_FORMAT);         /* a layer can't be grouped */
    CHECK_N(odg_group(&doc, ids, 0, NULL), OWF_ERR_FORMAT);
    CHECK_N(odg_group(&doc, ids, 1, NULL), OWF_OK);                    /* one shape makes a group, as draw.io */
    done();
}

static void test_round_trip(void)
{
    char g[ODG_ORDER_ID];
    const char *ids[2];
    unsigned char *data = NULL;
    size_t len = 0;
    owf_diagram back;
    const owf_dcell *gc;
    fresh();
    ids[0] = "B";
    ids[1] = "C";
    CHECK_N(odg_group(&doc, ids, 2, g), OWF_OK);
    CHECK_N(owf_drawio_write(&d, 0, &data, &len), OWF_OK);
    owf_diagram_init(&back);
    CHECK_N(owf_drawio_read(data, len, &back), OWF_OK);
    gc = owf_dpage_find(&back.pages[0], g);
    CHECK(gc && odg_is_group(gc));
    CHECK(owf_dpage_find(&back.pages[0], "C") && !strcmp(owf_dpage_find(&back.pages[0], "C")->parent, g));
    owf_diagram_free(&back);
    owf_free(data);
    done();
}

int main(void)
{
    test_front_and_back();
    test_one_step();
    test_reorder_refused();
    test_group();
    test_group_in_group_and_connectors();
    test_ungroup_container();
    test_round_trip();
    printf("diagram order and groups: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
