/*
 * GFOffice's one copy of shape geometry (libowf/src/owf_shape.c, owf_shape.h): its sine, its pen (parts and points,
 * turned and flipped into a box), its arcs, boxes, lines and ovals, and owf_shape_xy; and a golden sum of every
 * diagram shape's outline (owf_dshape_outline: each style word draw.io names a shape by, pointing four ways, flipped
 * each way, in two boxes, with and without a size) taken from main 3d40fb09, before the geometry moved out of
 * owf_dshape.c, so the move changed no point.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef OWF_SHAPE_GOLDEN_FROM_MAIN   /* the golden sum was taken with main 3d40fb09, which had no owf_shape.h */
#include "owf_shape.h"
#endif
#include "owf_diagram.h"

#include <stdio.h>
#include <string.h>

static int checks, failed;

static void check(int ok, const char *what)
{
    checks++;
    if (!ok) {
        failed++;
        printf("FAIL %s\n", what);
    }
}

/* the golden sum: main 3d40fb09's owf_dshape.c, before the move (test_owf_shape --sum) */
#ifndef GOLDEN_DRAWN
#define GOLDEN_DRAWN 7194
#define GOLDEN_SUM 0x2A3CAF87UL
#endif

static const char *const words[] = {
    "cylinder3", "cylinder", "mxgraph.flowchart.database", "cloud", "document", "mxgraph.flowchart.document",
    "mxgraph.flowchart.multi-document", "umlActor", "actor", "note", "card", "mxgraph.flowchart.card", "cube",
    "step", "trapezoid", "tape", "mxgraph.flowchart.paper_tape", "internalStorage",
    "mxgraph.flowchart.internal_storage", "callout", "dataStorage", "mxgraph.flowchart.stored_data", "manualInput",
    "mxgraph.flowchart.manual_input", "delay", "mxgraph.flowchart.delay", "display", "mxgraph.flowchart.display",
    "offPageConnector", "mxgraph.flowchart.off-page_reference", "cross", "sumEllipse",
    "mxgraph.flowchart.summing_function", "orEllipse", "mxgraph.flowchart.or", "loopLimit",
    "mxgraph.flowchart.loop_limit", "sortShape", "mxgraph.flowchart.sort", "collate", "mxgraph.flowchart.collate",
    "singleArrow", "doubleArrow", "umlLifeline", "umlBoundary", "umlEntity", "umlControl", "umlDestroy",
    "component", "module", "folder", "umlFrame", "requiredInterface", "providedRequiredInterface", "endState",
    "lollipop", "mxgraph.networks.server", "mxgraph.networks.pc", "mxgraph.networks.monitor",
    "mxgraph.networks.laptop", "mxgraph.networks.router", "mxgraph.networks.switch", "mxgraph.networks.hub",
    "mxgraph.networks.firewall", "mxgraph.networks.printer", "mxgraph.networks.mobile", "mxgraph.networks.tablet",
    "mxgraph.networks.modem", "mxgraph.networks.storage", "mxgraph.networks.mainframe", "mxgraph.networks.rack",
    "mxgraph.networks.patch_panel", "mxgraph.networks.user_male", "mxgraph.networks.cloud", "triangle", "rhombus",
    "ellipse",
};

/* FNV-1a over a shape's parts and points */
static unsigned long sum_shape(unsigned long h, const owf_dshape *s)
{
    int i;
    const long v[2] = { s->npoints, s->nparts };
    for (i = 0; i < 2; i++) h = (h ^ (unsigned long)v[i]) * 16777619UL & 0xFFFFFFFFUL;
    for (i = 0; i < s->nparts; i++) {
        h = (h ^ (unsigned long)s->parts[i].first) * 16777619UL & 0xFFFFFFFFUL;
        h = (h ^ (unsigned long)s->parts[i].n) * 16777619UL & 0xFFFFFFFFUL;
        h = (h ^ (unsigned long)s->parts[i].flags) * 16777619UL & 0xFFFFFFFFUL;
    }
    for (i = 0; i < s->npoints; i++) {
        h = (h ^ ((unsigned long)s->points[i].x & 0xFFFFFFFFUL)) * 16777619UL & 0xFFFFFFFFUL;
        h = (h ^ ((unsigned long)s->points[i].y & 0xFFFFFFFFUL)) * 16777619UL & 0xFFFFFFFFUL;
    }
    return h;
}

static unsigned long every_outline(int *drawn)
{
    static const char *const dirs[] = { "", "direction=south;", "direction=west;", "direction=north;" };
    static const char *const flips[] = { "", "flipH=1;", "flipV=1;", "flipH=1;flipV=1;" };
    static const char *const sizes[] = { "", "size=30;", "size=12;fixedSize=1;" };
    static const owf_dnum boxes[2][4] = { { 4000, 6000, 12000, 8000 }, { -2500, 300, 3100, 9700 } };
    unsigned long h = 2166136261UL;
    char style[160];
    owf_dshape s;
    int w, d, f, z, b;
    *drawn = 0;
    for (w = 0; w < (int)(sizeof words / sizeof words[0]); w++)
        for (d = 0; d < 4; d++)
            for (f = 0; f < 4; f++)
                for (z = 0; z < 3; z++)
                    for (b = 0; b < 2; b++) {
                        snprintf(style, sizeof style, "shape=%s;%s%s%swhiteSpace=wrap;", words[w], dirs[d], flips[f],
                                 sizes[z]);
                        if (owf_dshape_outline(style, boxes[b], &s)) {
                            (*drawn)++;
                            h = sum_shape(h, &s);
                        }
                    }
    return h;
}

#ifndef OWF_SHAPE_GOLDEN_FROM_MAIN
/* the pen, the primitives and owf_shape_xy */
static void primitives(void)
{
    static const owf_snum box[4] = { 1000, 2000, 400, 200 };
    owf_shape s;
    owf_shape_pen pen;
    int xy[16], n, i, ok;

    check(owf_shape_sin64(0) == 0 && owf_shape_sin64(16) == 10000 && owf_shape_sin64(32) == 0 &&
          owf_shape_sin64(48) == -10000 && owf_shape_sin64(8) == 7071 && owf_shape_sin64(-8) == -7071 &&
          owf_shape_sin64(72) == 7071 && owf_shape_cos64(0) == 10000 && owf_shape_cos64(32) == -10000,
          "the sine and cosine, a 64th of a turn at a time");

    owf_shape_pen_init(&pen, &s, box, 0, 0, 0);
    owf_shape_rect(&pen, OWF_SHAPE_CLOSED | OWF_SHAPE_FILL, 0, 0, 100, 50);
    check(s.nparts == 1 && s.npoints == 4 && s.parts[0].first == 0 && s.parts[0].n == 4 &&
          s.parts[0].flags == (OWF_SHAPE_CLOSED | OWF_SHAPE_FILL) && s.points[0].x == 1000 &&
          s.points[0].y == 2000 && s.points[2].x == 1100 && s.points[2].y == 2050, "a box, into the box's place");
    owf_shape_line(&pen, 0, 0, 0, 400, 200);
    check(s.nparts == 2 && s.parts[1].flags == OWF_SHAPE_LINE && s.parts[1].n == 2 && s.points[5].x == 1400 &&
          s.points[5].y == 2200, "a line, stroked");

    /* turned: built pointing east w across (the box's height when turned a quarter), then into the box */
    owf_shape_pen_init(&pen, &s, box, 1, 0, 0);
    check(pen.w == 200 && pen.h == 400, "turned south: built 200 across, 400 down");
    owf_shape_start(&pen, OWF_SHAPE_LINE);
    owf_shape_point(&pen, 0, 0);
    owf_shape_point(&pen, 200, 0);
    check(s.points[0].x == 1400 && s.points[0].y == 2000 && s.points[1].x == 1400 && s.points[1].y == 2200,
          "turned south, east points down");
    owf_shape_pen_init(&pen, &s, box, 0, 1, 1);
    owf_shape_start(&pen, OWF_SHAPE_LINE);
    owf_shape_point(&pen, 10, 20);
    check(s.points[0].x == 1390 && s.points[0].y == 2180, "flipped both ways");

    owf_shape_pen_init(&pen, &s, box, 0, 0, 0);
    owf_shape_oval(&pen, OWF_SHAPE_CLOSED, 200, 100, 200, 100, 16);
    check(s.nparts == 1 && s.npoints == 4 && s.points[0].x == 1400 && s.points[0].y == 2100 && s.points[1].x == 1200 &&
          s.points[1].y == 2200 && s.points[3].x == 1200 && s.points[3].y == 2000, "an oval, a point a quarter turn");
    owf_shape_start(&pen, OWF_SHAPE_LINE);
    owf_shape_arc(&pen, 0, 0, 100, 100, 16, 0, 16);
    check(s.parts[1].n == 2 && s.points[4].x == 1000 && s.points[4].y == 2100 && s.points[5].x == 1100 &&
          s.points[5].y == 2000, "an arc drawn backwards");

    n = owf_shape_xy(&s, 0, 1, 10, 5, -5, xy, 8);
    check(n == 4 && xy[0] == 145 && xy[1] == 205 && xy[2] == 125 && xy[3] == 215, "owf_shape_xy: scaled and moved");
    check(owf_shape_xy(&s, 0, 1, 10, 0, 0, xy, 2) == 2 && owf_shape_xy(&s, 2, 1, 1, 0, 0, xy, 8) == 0 &&
          owf_shape_xy(&s, 0, 1, 0, 0, 0, xy, 8) == 0 && owf_shape_xy(&s, -1, 1, 1, 0, 0, xy, 8) == 0,
          "owf_shape_xy: at most max points, and none for a part it hasn't or a zero div");

    owf_shape_pen_init(&pen, &s, box, 0, 0, 0);
    owf_shape_point(&pen, 1, 1);
    check(pen.full && s.npoints == 0, "a point before any part is refused");
    owf_shape_pen_init(&pen, &s, box, 0, 0, 0);
    for (i = 0; i < OWF_SHAPE_PARTS + 1; i++)
        owf_shape_start(&pen, 0);
    check(pen.full && s.nparts == OWF_SHAPE_PARTS, "a part past the last is refused");
    owf_shape_pen_init(&pen, &s, box, 0, 0, 0);
    owf_shape_start(&pen, 0);
    for (i = 0; i < OWF_SHAPE_POINTS + 3; i++)
        owf_shape_point(&pen, i, i);
    ok = pen.full && s.npoints == OWF_SHAPE_POINTS && s.parts[0].n == OWF_SHAPE_POINTS;
    check(ok, "a point past the last is refused");
    check(sizeof(owf_dshape) == sizeof(owf_shape) && sizeof(owf_dpoint) == sizeof(owf_spoint) &&
          OWF_DSHAPE_POINTS == OWF_SHAPE_POINTS && OWF_DSHAPE_LINE == OWF_SHAPE_LINE,
          "the diagram's shapes are owf_shape's");
}
#endif

int main(int argc, char **argv)
{
    int drawn;
    unsigned long h = every_outline(&drawn);
    if (argc > 1 && !strcmp(argv[1], "--sum")) {         /* how the golden sum was taken */
        printf("%d outlines, sum %08lX\n", drawn, h);
        return 0;
    }
#ifndef OWF_SHAPE_GOLDEN_FROM_MAIN
    primitives();
#endif
    check(drawn == GOLDEN_DRAWN && h == GOLDEN_SUM, "every diagram shape's outline as before the move");
    printf("shared shapes: %d checks, %d failed (%d outlines, sum %08lX)\n", checks, failed, drawn, h);
    return failed != 0;
}
