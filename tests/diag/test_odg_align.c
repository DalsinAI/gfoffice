/*
 * GFOpenDiag's Align and Distribute on the PC (src/diag/odg_align.c): each is one Undo step, shapes line up where
 * they are seen on the page (inside groups too), and orthogonal connectors follow. Every expected place below was
 * worked out by hand. Run under ASan and UBSan by make check.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <stdio.h>
#include <string.h>
#include "opendiag_align.h"

static int fails, checks;

#define CHECK(cond) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_N(got, want) do { long g_ = (long)(got), w_ = (long)(want); checks++; if (g_ != w_) { fails++; \
    printf("FAIL %s:%d: %s is %ld, want %ld\n", __FILE__, __LINE__, #got, g_, w_); } } while (0)

#define PX(n) OWF_DNUM(n)

static owf_diagram d;
static odg_doc doc;

static void put(const char *id, const char *parent, long x, long y, long w, long h)
{
    owf_dcell *c = owf_dpage_add(odg_page(&doc), id, parent, OWF_DCELL_VERTEX);
    c->geo.flags = OWF_DGEO_HAS;
    c->geo.x = PX(x);
    c->geo.y = PX(y);
    c->geo.width = PX(w);
    c->geo.height = PX(h);
}

/* A 10,10 40x20; B 100,50 60x40; C 300,0 20x80; and group G at 200,200 holding D at 10,10 (so 210,210) 40x40. */
static void fresh(void)
{
    owf_diagram_init(&d);
    owf_diagram_add_page(&d, "p", "Page-1");
    odg_init(&doc, &d, 0);
    put("A", "1", 10, 10, 40, 20);
    put("B", "1", 100, 50, 60, 40);
    put("C", "1", 300, 0, 20, 80);
    put("G", "1", 200, 200, 100, 100);
    owf_dcell_set(&owf_dpage_find(odg_page(&doc), "G")->style, "group;");
    put("D", "G", 10, 10, 40, 40);
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

static void test_align_across(void)
{
    const char *ids[4] = { "A", "B", "C", "D" };
    int before;
    fresh();
    before = doc.nundo;
    CHECK_N(odg_align(&doc, ids, 4, ODG_ALIGN_LEFT), OWF_OK);
    CHECK_N(doc.nundo, before + 1);
    CHECK_N(cell("A")->geo.x, PX(10));
    CHECK_N(cell("B")->geo.x, PX(10));
    CHECK_N(cell("C")->geo.x, PX(10));
    CHECK_N(cell("D")->geo.x, PX(-190));        /* 10 on the page, inside G at 200 */
    CHECK_N(cell("G")->geo.x, PX(200));
    CHECK_N(cell("B")->geo.y, PX(50));          /* across only */
    CHECK_N(odg_align(&doc, ids, 4, ODG_ALIGN_LEFT), OWF_ERR_FORMAT);  /* nothing moves: no step */
    CHECK_N(doc.nundo, before + 1);
    CHECK_N(odg_undo(&doc), OWF_OK);
    CHECK_N(cell("B")->geo.x, PX(100));

    CHECK_N(odg_align(&doc, ids, 3, ODG_ALIGN_RIGHT), OWF_OK);
    CHECK_N(cell("A")->geo.x, PX(280));         /* right sides at 320 */
    CHECK_N(cell("B")->geo.x, PX(260));
    CHECK_N(cell("C")->geo.x, PX(300));
    CHECK_N(odg_undo(&doc), OWF_OK);

    CHECK_N(odg_align(&doc, ids, 3, ODG_ALIGN_CENTRE), OWF_OK);
    CHECK_N(cell("A")->geo.x, PX(145));         /* round them 10 to 320: middle 165 */
    CHECK_N(cell("B")->geo.x, PX(135));
    CHECK_N(cell("C")->geo.x, PX(155));
    done();
}

static void test_align_down(void)
{
    const char *ids[3] = { "A", "B", "C" };
    fresh();
    CHECK_N(odg_align(&doc, ids, 3, ODG_ALIGN_TOP), OWF_OK);
    CHECK_N(cell("A")->geo.y, PX(0));
    CHECK_N(cell("B")->geo.y, PX(0));
    CHECK_N(cell("A")->geo.x, PX(10));
    CHECK_N(odg_undo(&doc), OWF_OK);
    CHECK_N(odg_align(&doc, ids, 3, ODG_ALIGN_BOTTOM), OWF_OK);
    CHECK_N(cell("A")->geo.y, PX(70));          /* bottoms at 90 */
    CHECK_N(cell("C")->geo.y, PX(10));
    CHECK_N(odg_undo(&doc), OWF_OK);
    CHECK_N(odg_align(&doc, ids, 3, ODG_ALIGN_MIDDLE), OWF_OK);
    CHECK_N(cell("A")->geo.y, PX(35));          /* round them 0 to 90: middle 45 */
    CHECK_N(cell("B")->geo.y, PX(25));
    CHECK_N(cell("C")->geo.y, PX(5));
    done();
}

static void test_refused(void)
{
    const char *ids[3] = { "A", "1", "nope" };
    fresh();
    CHECK_N(odg_align(&doc, ids, 3, ODG_ALIGN_LEFT), OWF_ERR_FORMAT);  /* one shape only */
    CHECK_N(odg_align(&doc, ids, 1, ODG_ALIGN_LEFT), OWF_ERR_FORMAT);
    ids[1] = "B";
    CHECK_N(odg_align(&doc, ids, 2, (odg_align_how)17), OWF_ERR_FORMAT);
    CHECK_N(odg_distribute(&doc, ids, 3, 1), OWF_ERR_FORMAT);          /* two shapes: nothing between */
    ids[0] = "G";
    ids[1] = "D";                               /* D inside G moves with G: one shape */
    CHECK_N(odg_align(&doc, ids, 2, ODG_ALIGN_LEFT), OWF_ERR_FORMAT);
    CHECK_N(doc.nundo, 0);
    done();
}

static void test_distribute(void)
{
    const char *ids[4] = { "C", "A", "B", "D" };
    int before;
    fresh();
    before = doc.nundo;
    /* middles across: A 30, B 130, C 310, D 230. In order A, B, D, C: from 30 to 310 in steps of 93.33 */
    CHECK_N(odg_distribute(&doc, ids, 4, 1), OWF_OK);
    CHECK_N(doc.nundo, before + 1);
    CHECK_N(cell("A")->geo.x, PX(10));          /* the ends stay */
    CHECK_N(cell("C")->geo.x, PX(300));
    CHECK_N(cell("B")->geo.x, 12333 - 3000);    /* middle 123.33 */
    CHECK_N(cell("D")->geo.x, 21666 - 2000 - 20000);   /* middle 216.66, inside G */
    CHECK_N(odg_distribute(&doc, ids, 4, 1), OWF_ERR_FORMAT);          /* even already */
    CHECK_N(odg_undo(&doc), OWF_OK);
    /* down, three: A 20, B 70, C 40: in order A, C, B; C's middle to 45 */
    CHECK_N(odg_distribute(&doc, ids, 3, 0), OWF_OK);
    CHECK_N(cell("C")->geo.y, PX(5));
    CHECK_N(cell("A")->geo.y, PX(10));
    CHECK_N(cell("B")->geo.y, PX(50));
    done();
}

static void test_connectors_follow(void)
{
    const char *ids[2] = { "A", "B" };
    char e[24];
    owf_dpoint *pts;
    int n = 0;
    fresh();
    CHECK_N(odg_add_edge(&doc, "A", "B", NULL, NULL, "edgeStyle=orthogonalEdgeStyle;", e), OWF_OK);
    CHECK(cell(e)->geo.npoints > 0);            /* a step down from A to B */
    CHECK_N(odg_align(&doc, ids, 2, ODG_ALIGN_MIDDLE), OWF_OK);
    pts = owf_dedge_path(odg_page(&doc), cell(e), &n);
    CHECK(pts != NULL && n >= 2);
    if (pts && n >= 2) {
        int k;
        for (k = 0; k < n; k++)
            CHECK_N(pts[k].y, PX(50));          /* both middles at 50 now: routed again, level */
    }
    owf_free(pts);
    done();
}

int main(void)
{
    test_align_across();
    test_align_down();
    test_refused();
    test_distribute();
    test_connectors_follow();
    printf("diagram align and distribute: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
