/*
 * The shapes' outlines on the PC (libowf/diagram/owf_dshape.c): every shape word (basic, flowchart, UML and
 * network) gives an outline that fills its box and stays inside it, turned and flipped shapes land where they
 * should, and the names go both ways. The places below were worked out by hand. Run under ASan and UBSan by make
 * check.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <stdio.h>
#include <string.h>
#include "owf_diagram.h"

static int fails, checks;

#define CHECK(cond) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_N(got, want) do { long g_ = (long)(got), w_ = (long)(want); checks++; if (g_ != w_) { fails++; \
    printf("FAIL %s:%d: %s is %ld, want %ld\n", __FILE__, __LINE__, #got, g_, w_); } } while (0)
#define CHECK_S(got, want) do { const char *g_ = (got), *w_ = (want); checks++; if (!g_ || strcmp(g_, w_) != 0) { \
    fails++; printf("FAIL %s:%d: %s is \"%s\", want \"%s\"\n", __FILE__, __LINE__, #got, g_ ? g_ : "(null)", w_); } } while (0)

#define PX(n) OWF_DNUM(n)

static owf_dshape s;

/* Is there a point at x, y (page pixels)? */
static int has(long x, long y)
{
    int i;
    for (i = 0; i < s.npoints; i++)
        if (s.points[i].x == PX(x) && s.points[i].y == PX(y))
            return 1;
    return 0;
}

/* Every shape: inside its box, each part long enough, and (but the cloud, which is rounder) out to every side. */
static void test_every_shape(void)
{
    static const char *const styles[] = {
        "shape=cylinder3;whiteSpace=wrap;html=1;boundedLbl=1;size=15;", "shape=cylinder;", "shape=cloud;",
        "ellipse;shape=cloud;whiteSpace=wrap;html=1;", "shape=document;whiteSpace=wrap;html=1;boundedLbl=1;",
        "shape=mxgraph.flowchart.multi-document;", "shape=umlActor;verticalLabelPosition=bottom;",
        "shape=note;size=20;", "shape=card;", "shape=cube;", "shape=step;perimeter=stepPerimeter;",
        "shape=trapezoid;", "shape=tape;", "shape=internalStorage;", "shape=callout;", "shape=dataStorage;",
        "shape=manualInput;", "shape=delay;", "shape=display;", "shape=offPageConnector;", "shape=cross;",
        "shape=sumEllipse;", "shape=orEllipse;", "shape=loopLimit;", "shape=sortShape;", "shape=collate;",
        "shape=singleArrow;", "shape=doubleArrow;", "triangle;direction=south;", "shape=mxgraph.flowchart.delay;",
        "shape=mxgraph.flowchart.or;", "shape=mxgraph.flowchart.summing_function;",
        "shape=mxgraph.flowchart.off-page_reference;", "shape=mxgraph.flowchart.paper_tape;",
        "shape=umlLifeline;perimeter=lifelinePerimeter;size=40;", "shape=umlBoundary;", "shape=umlEntity;",
        "shape=umlControl;", "shape=umlDestroy;", "shape=component;", "shape=module;jettyWidth=10;",
        "shape=folder;tabWidth=40;tabHeight=14;", "shape=umlFrame;width=60;height=20;", "shape=requiredInterface;",
        "shape=providedRequiredInterface;", "shape=endState;", "shape=lollipop;",
        "shape=mxgraph.networks.server;", "shape=mxgraph.networks.pc;", "shape=mxgraph.networks.monitor;",
        "shape=mxgraph.networks.laptop;", "shape=mxgraph.networks.router;", "shape=mxgraph.networks.switch;",
        "shape=mxgraph.networks.hub;", "shape=mxgraph.networks.firewall;", "shape=mxgraph.networks.printer;",
        "shape=mxgraph.networks.mobile;", "shape=mxgraph.networks.tablet;", "shape=mxgraph.networks.modem;",
        "shape=mxgraph.networks.storage;", "shape=mxgraph.networks.mainframe;", "shape=mxgraph.networks.rack;",
        "shape=mxgraph.networks.patch_panel;", "shape=mxgraph.networks.user_male;",
        "shape=mxgraph.networks.cloud;", "shape=mxgraph.networks.router;direction=south;",
    };
    owf_dnum box[4] = { PX(20), PX(30), PX(120), PX(80) };
    int i, k, j;
    for (i = 0; i < (int)(sizeof styles / sizeof styles[0]); i++) {
        owf_dnum lo_x = 0, hi_x = 0, lo_y = 0, hi_y = 0;
        int ok = owf_dshape_outline(styles[i], box, &s), cloud = strstr(styles[i], "cloud") != NULL;
        checks++;
        if (ok != 1 || s.nparts < 1) {
            fails++;
            printf("FAIL: %s gave %d with %d parts\n", styles[i], ok, s.nparts);
            continue;
        }
        for (k = 0; k < s.nparts; k++) {
            const owf_dshape_part *pt = &s.parts[k];
            checks++;
            if (pt->n < ((pt->flags & OWF_DSHAPE_CLOSED) ? 3 : 2) ||
                !(pt->flags & (OWF_DSHAPE_FILL | OWF_DSHAPE_LINE | OWF_DSHAPE_INK)) ||
                pt->first + pt->n > s.npoints) {
                fails++;
                printf("FAIL: %s part %d: %d points, flags %u\n", styles[i], k, pt->n, pt->flags);
            }
        }
        for (j = 0; j < s.npoints; j++) {
            owf_dnum x = s.points[j].x, y = s.points[j].y;
            if (!j || x < lo_x) lo_x = x;
            if (!j || x > hi_x) hi_x = x;
            if (!j || y < lo_y) lo_y = y;
            if (!j || y > hi_y) hi_y = y;
        }
        checks++;
        if (lo_x < box[0] || hi_x > box[0] + box[2] || lo_y < box[1] || hi_y > box[1] + box[3]) {
            fails++;
            printf("FAIL: %s goes outside its box: %ld,%ld to %ld,%ld\n", styles[i], lo_x, lo_y, hi_x, hi_y);
        }
        checks++;
        if (!cloud && (lo_x > box[0] + 1 || hi_x < box[0] + box[2] - 1 || lo_y > box[1] + 1 ||
                       hi_y < box[1] + box[3] - 1)) {
            fails++;
            printf("FAIL: %s doesn't reach its box's sides: %ld,%ld to %ld,%ld\n", styles[i], lo_x, lo_y, hi_x, hi_y);
        }
        checks++;
        if (cloud && (hi_x - lo_x < box[2] * 85 / 100 || hi_y - lo_y < box[3] * 85 / 100)) {
            fails++;
            printf("FAIL: %s is too small\n", styles[i]);
        }
    }
}

static void test_places(void)
{
    owf_dnum box[4] = { PX(10), PX(10), PX(100), PX(60) };
    CHECK_N(owf_dshape_outline("shape=note;size=20;", box, &s), 1);
    CHECK_N(s.nparts, 2);
    CHECK_N(s.parts[0].flags, OWF_DSHAPE_CLOSED | OWF_DSHAPE_FILL | OWF_DSHAPE_LINE);
    CHECK_N(s.parts[1].flags, OWF_DSHAPE_LINE);
    CHECK(has(10, 10) && has(90, 10) && has(110, 30) && has(110, 70) && has(10, 70));
    CHECK_N(s.parts[1].n, 3);                  /* the fold */
    CHECK_N(s.points[s.parts[1].first + 1].x, PX(90));
    CHECK_N(s.points[s.parts[1].first + 1].y, PX(30));

    CHECK_N(owf_dshape_outline("shape=step;", box, &s), 1);        /* size 0.2 of the width */
    CHECK(has(10, 10) && has(90, 10) && has(110, 40) && has(90, 70) && has(10, 70) && has(30, 40));
    CHECK_N(owf_dshape_outline("shape=step;fixedSize=1;size=15;", box, &s), 1);   /* in pixels with fixedSize */
    CHECK(has(95, 10) && has(110, 40) && has(25, 40));
    CHECK_N(owf_dshape_outline("shape=step;fixedSize=1;", box, &s), 1);           /* 20 when not given */
    CHECK(has(90, 10) && has(30, 40));
    CHECK_N(owf_dshape_outline("shape=offPageConnector;", box, &s), 1);   /* the point 0.38 of the height */
    CHECK(has(10, 10) && has(110, 10) && has(60, 70));
    CHECK_N(s.points[2].y, 1000 + 6000 - 2280);
    CHECK_N(owf_dshape_outline("shape=manualInput;size=15;", box, &s), 1);
    CHECK(has(10, 25) && has(110, 10) && has(110, 70) && has(10, 70));
    CHECK_N(owf_dshape_outline("shape=card;size=200;", box, &s), 1);   /* the cut no more than the shorter side */
    CHECK(has(70, 10) && has(10, 70));
    CHECK_N(owf_dshape_outline("shape=cylinder3;size=10;", box, &s), 1);
    CHECK_N(s.nparts, 2);
    CHECK(has(10, 20) && has(110, 20) && has(60, 10) && has(60, 70) && has(110, 60) && has(10, 60));
    CHECK(has(60, 30));                        /* the top's near edge */
    CHECK_N(owf_dshape_outline("shape=umlActor;", box, &s), 1);
    CHECK_N(s.nparts, 4);
    CHECK(has(60, 10) && has(10, 30) && has(110, 30) && has(10, 70) && has(110, 70) && has(60, 50));
}

static void test_turned(void)
{
    owf_dnum box[4] = { PX(0), PX(0), PX(40), PX(80) };
    CHECK_N(owf_dshape_outline("triangle;direction=south;", box, &s), 1);   /* pointing down */
    CHECK(has(40, 0) && has(20, 80) && has(0, 0));
    CHECK_N(owf_dshape_outline("triangle;direction=north;", box, &s), 1);
    CHECK(has(0, 80) && has(20, 0) && has(40, 80));
    CHECK_N(owf_dshape_outline("triangle;direction=west;", box, &s), 1);
    CHECK(has(40, 0) && has(0, 40) && has(40, 80));
    CHECK_N(owf_dshape_outline("triangle;flipH=1;", box, &s), 1);           /* flipped is west too */
    CHECK(has(40, 0) && has(0, 40) && has(40, 80));
    CHECK_N(owf_dshape_outline("triangle;", box, &s), 0);                   /* the old drawing code has it */
    CHECK_N(s.npoints, 0);
    CHECK_N(owf_dshape_outline("triangle;direction=east;", box, &s), 0);
    CHECK_N(owf_dshape_outline("shape=manualInput;size=10;flipV=1;", box, &s), 1);   /* sloping down */
    CHECK(has(0, 70) && has(40, 80) && has(40, 0) && has(0, 0));
    CHECK_N(owf_dshape_outline("shape=step;direction=south;", box, &s), 1);  /* 0.2 of its length, now down */
    CHECK(has(40, 0) && has(40, 64) && has(20, 80) && has(0, 64) && has(0, 0) && has(20, 16));
}

static void test_names(void)
{
    owf_dnum box[4] = { 0, 0, PX(10), PX(10) }, none[4] = { 0, 0, 0, PX(10) };
    CHECK_S(owf_dshape_name("shape=cylinder;"), "cylinder3");
    CHECK_S(owf_dshape_name("shape=cylinder3;size=15;"), "cylinder3");
    CHECK_S(owf_dshape_name("shape=mxgraph.flowchart.delay;"), "delay");
    CHECK_S(owf_dshape_name("shape=mxgraph.flowchart.multi-document;"), "mxgraph.flowchart.multi-document");
    CHECK_S(owf_dshape_name("ellipse;shape=cloud;"), "cloud");
    CHECK_S(owf_dshape_name("cloud;"), "cloud");
    CHECK_S(owf_dshape_name("shape=actor;"), "umlActor");
    CHECK_S(owf_dshape_name("triangle;direction=north;"), "triangle");
    CHECK(owf_dshape_name("ellipse;") == NULL);
    CHECK(owf_dshape_name("rounded=1;whiteSpace=wrap;") == NULL);
    CHECK(owf_dshape_name("triangle;") == NULL);
    CHECK(owf_dshape_name("shape=mxgraph.cisco.router;") == NULL);
    CHECK_S(owf_dshape_name("shape=module;"), "component");
    CHECK_S(owf_dshape_name("shape=umlLifeline;size=40;"), "umlLifeline");
    CHECK_S(owf_dshape_name("shape=mxgraph.networks.patch_panel;"), "mxgraph.networks.patch_panel");
    CHECK_S(owf_dshape_name("shape=mxgraph.networks.cloud;"), "cloud");
    CHECK(owf_dshape_name("shape=mxgraph.networks.toaster;") == NULL);
    CHECK(owf_dshape_name(NULL) == NULL);
    CHECK_N(owf_dshape_outline(NULL, box, &s), 0);
    CHECK_N(owf_dshape_outline("shape=rhombus;", box, &s), 0);
    CHECK_N(owf_dshape_outline("shape=note;", none, &s), 0);               /* no width */
    CHECK_N(owf_dshape_outline("shape=note;", box, &s), 1);                /* a tiny one still fits */
    CHECK(has(0, 0) && has(10, 10));
}

/* UML and the network set: the places that make each what it is. */
static void test_uml_network(void)
{
    owf_dnum box[4] = { PX(10), PX(10), PX(100), PX(60) };
    CHECK_N(owf_dshape_outline("shape=umlLifeline;size=20;", box, &s), 1);   /* a box, a dashed line down */
    CHECK_N(s.nparts, 2);
    CHECK(has(10, 10) && has(110, 30) && has(60, 30) && has(60, 70));
    CHECK_N(s.parts[1].flags, OWF_DSHAPE_LINE | OWF_DSHAPE_DASHED);
    CHECK_N(owf_dshape_outline("shape=folder;tabWidth=30;tabHeight=10;", box, &s), 1);
    CHECK(has(10, 10) && has(40, 10) && has(40, 20) && has(110, 20) && has(110, 70) && has(10, 70));
    CHECK_N(owf_dshape_outline("shape=folder;tabWidth=500;", box, &s), 1);   /* the tab no wider than the box */
    CHECK(has(110, 10) && has(110, 30));
    CHECK_N(owf_dshape_outline("shape=umlFrame;width=40;height=20;", box, &s), 1);   /* the name's corner */
    CHECK(has(10, 30) && has(40, 30) && has(50, 20) && has(50, 10));
    CHECK_N(owf_dshape_outline("shape=component;jettyWidth=20;jettyHeight=10;", box, &s), 1);
    CHECK_N(s.nparts, 3);
    CHECK(has(20, 10) && has(110, 70) && has(10, 28) && has(30, 38) && has(10, 46) && has(30, 56));
    CHECK_N(owf_dshape_outline("shape=umlDestroy;", box, &s), 1);
    CHECK(has(10, 10) && has(110, 70) && has(110, 10) && has(10, 70));
    CHECK_N(owf_dshape_outline("shape=endState;", box, &s), 1);       /* a ring round a dot in the line colour */
    CHECK_N(s.nparts, 2);
    CHECK_N(s.parts[1].flags, OWF_DSHAPE_CLOSED | OWF_DSHAPE_INK | OWF_DSHAPE_LINE);
    CHECK(has(110, 40) && has(90, 40));
    CHECK_N(owf_dshape_outline("shape=umlBoundary;", box, &s), 1);
    CHECK(has(10, 10) && has(10, 70) && has(35, 40) && has(110, 40));
    CHECK_N(owf_dshape_outline("shape=requiredInterface;", box, &s), 1);   /* a socket open to the left */
    CHECK(has(10, 10) && has(110, 40) && has(10, 70));

    CHECK_N(owf_dshape_outline("shape=mxgraph.networks.firewall;", box, &s), 1);   /* bricks, joints staggered */
    CHECK_N(s.nparts, 7);
    CHECK_N(s.points[s.parts[3].first].x, PX(60));
    CHECK_N(s.points[s.parts[4].first].x, PX(35));
    CHECK_N(owf_dshape_outline("shape=mxgraph.networks.switch;", box, &s), 1);   /* front, top, side, arrows */
    CHECK_N(s.nparts, 5);
    CHECK(has(10, 30) && has(30, 10) && has(110, 10) && has(90, 70) && has(110, 50));
    CHECK_N(s.parts[3].n, 5);
    CHECK_N(owf_dshape_outline("shape=mxgraph.networks.server;", box, &s), 1);
    CHECK_N(s.parts[3].flags & OWF_DSHAPE_INK, OWF_DSHAPE_INK);       /* the button */
    CHECK_N(owf_dshape_outline("shape=mxgraph.networks.patch_panel;", box, &s), 1);
    CHECK_N(s.nparts, 7);
    CHECK_N(owf_dshape_outline("shape=mxgraph.networks.storage;", box, &s), 1);   /* three disks, two parts each */
    CHECK_N(s.nparts, 6);
    CHECK(has(60, 10) && has(60, 70));
    CHECK_N(owf_dshape_outline("shape=mxgraph.networks.laptop;", box, &s), 1);
    CHECK(has(20, 10) && has(100, 55) && has(110, 70) && has(10, 70));
}

int main(void)
{
    test_every_shape();
    test_places();
    test_turned();
    test_names();
    test_uml_network();
    printf("diagram shapes: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
