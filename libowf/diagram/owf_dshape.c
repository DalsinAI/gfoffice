/*
 * owf_dshape.c: the outlines of GFOpenDiag's shapes beyond the first flowchart set (owf_diagram.h, "Shapes"),
 * worked out once for the canvas and every writer (SVG, PDF, EPS) to draw: closed outlines to fill and stroke,
 * and lines to stroke. Integer arithmetic only: curves are runs of short lines from a 64-step sine table.
 * The shapes are drawn in our own way from their plain geometric descriptions; only their names (draw.io's
 * style words, "shape=cylinder3", "shape=note", and its direction, flipH and flipV keys) are taken from the
 * .drawio format, so that a file made here opens in diagrams.net as the same shapes and one made there opens
 * here. No other program's code.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "owf_diag_int.h"

#include <string.h>

typedef long long wide;

/* sin of k/64 of a turn, k 0 to 16 (a quarter), times 10000 */
static const int quarter[17] = { 0, 980, 1951, 2903, 3827, 4714, 5556, 6344, 7071, 7730, 8315, 8819, 9239, 9569,
                                 9808, 9952, 10000 };

/* sin of a/64 of a turn, times 10000; a turns clockwise on the page (y down), 0 east, 16 south */
static int sin64(int a)
{
    a %= 64;
    if (a < 0)
        a += 64;
    if (a <= 16) return quarter[a];
    if (a <= 32) return quarter[32 - a];
    if (a <= 48) return -quarter[a - 32];
    return -quarter[64 - a];
}

static int cos64(int a)
{
    return sin64(a + 16);
}

/* ---- The table of shapes ---- */

enum {
    S_NONE, S_CYLINDER, S_CLOUD, S_DOCUMENT, S_MULTIDOC, S_ACTOR, S_NOTE, S_CARD, S_CUBE, S_STEP, S_TRAPEZOID,
    S_TAPE, S_INTERNAL, S_CALLOUT, S_DATASTORAGE, S_MANUALINPUT, S_DELAY, S_DISPLAY, S_OFFPAGE, S_CROSS, S_SUM,
    S_OR, S_LOOPLIMIT, S_SORT, S_COLLATE, S_TRIANGLE, S_ARROW, S_DOUBLEARROW
};

typedef struct {
    const char *word;                    /* the style's shape word, or a bare word ("cloud;") */
    int kind;
} shape_word;

/* Every word a .drawio file may name each shape by; the first of each kind is the one GFOpenDiag writes. */
static const shape_word words[] = {
    { "cylinder3", S_CYLINDER }, { "cylinder", S_CYLINDER }, { "mxgraph.flowchart.database", S_CYLINDER },
    { "cloud", S_CLOUD },
    { "document", S_DOCUMENT }, { "mxgraph.flowchart.document", S_DOCUMENT },
    { "mxgraph.flowchart.multi-document", S_MULTIDOC },
    { "umlActor", S_ACTOR }, { "actor", S_ACTOR },
    { "note", S_NOTE }, { "card", S_CARD }, { "mxgraph.flowchart.card", S_CARD },
    { "cube", S_CUBE },
    { "step", S_STEP }, { "trapezoid", S_TRAPEZOID },
    { "tape", S_TAPE }, { "mxgraph.flowchart.paper_tape", S_TAPE },
    { "internalStorage", S_INTERNAL }, { "mxgraph.flowchart.internal_storage", S_INTERNAL },
    { "callout", S_CALLOUT },
    { "dataStorage", S_DATASTORAGE }, { "mxgraph.flowchart.stored_data", S_DATASTORAGE },
    { "manualInput", S_MANUALINPUT }, { "mxgraph.flowchart.manual_input", S_MANUALINPUT },
    { "delay", S_DELAY }, { "mxgraph.flowchart.delay", S_DELAY },
    { "display", S_DISPLAY }, { "mxgraph.flowchart.display", S_DISPLAY },
    { "offPageConnector", S_OFFPAGE }, { "mxgraph.flowchart.off-page_reference", S_OFFPAGE },
    { "cross", S_CROSS },
    { "sumEllipse", S_SUM }, { "mxgraph.flowchart.summing_function", S_SUM },
    { "orEllipse", S_OR }, { "mxgraph.flowchart.or", S_OR },
    { "loopLimit", S_LOOPLIMIT }, { "mxgraph.flowchart.loop_limit", S_LOOPLIMIT },
    { "sortShape", S_SORT }, { "mxgraph.flowchart.sort", S_SORT },
    { "collate", S_COLLATE }, { "mxgraph.flowchart.collate", S_COLLATE },
    { "singleArrow", S_ARROW }, { "doubleArrow", S_DOUBLEARROW },
};

#define NWORDS ((int)(sizeof words / sizeof words[0]))

/* The kind a style names; the triangle only when it is turned or flipped (else the old drawing code has it). */
static int kind_of(const char *style, int *turned)
{
    char shape[64], v[16];
    int i;
    *turned = 0;
    if (!style)
        return S_NONE;
    *turned = (owf_dstyle_get(style, "direction", v, sizeof v) && v[0] && strcmp(v, "east")) ||
              (owf_dstyle_get(style, "flipH", v, sizeof v) && !strcmp(v, "1")) ||
              (owf_dstyle_get(style, "flipV", v, sizeof v) && !strcmp(v, "1"));
    if (owf_dstyle_get(style, "shape", shape, sizeof shape)) {
        for (i = 0; i < NWORDS; i++)
            if (!strcmp(shape, words[i].word))
                return words[i].kind;
        if (!strcmp(shape, "triangle"))
            return *turned ? S_TRIANGLE : S_NONE;
        return S_NONE;
    }
    for (i = 0; i < NWORDS; i++)         /* a bare word: "cloud;", as draw.io writes a few */
        if (owf_dstyle_get(style, words[i].word, v, sizeof v) && !v[0])
            return words[i].kind;
    if (owf_dstyle_get(style, "triangle", v, sizeof v) && !v[0] && *turned)
        return S_TRIANGLE;
    return S_NONE;
}

const char *owf_dshape_name(const char *style)
{
    int turned, kind = kind_of(style, &turned), i;
    if (kind == S_TRIANGLE)
        return "triangle";
    for (i = 0; i < NWORDS; i++)
        if (words[i].kind == kind)
            return words[i].word;
    return NULL;
}

/* ---- Building an outline ---- */

typedef struct {
    owf_dshape *s;
    owf_dnum bx, by, bw, bh;             /* the box on the page */
    owf_dnum w, h;                       /* the shape as built, pointing east: w across, h down */
    int turn;                            /* quarter turns clockwise: 0 east, 1 south, 2 west, 3 north */
    int flip_h, flip_v;
    int full;                            /* out of room */
} build;

static void start(build *b, unsigned flags)
{
    owf_dshape *s = b->s;
    if (s->nparts == OWF_DSHAPE_PARTS) {
        b->full = 1;
        return;
    }
    s->parts[s->nparts].first = s->npoints;
    s->parts[s->nparts].n = 0;
    s->parts[s->nparts].flags = flags;
    s->nparts++;
}

/* A point of the shape as built, onto the page. */
static void pt(build *b, owf_dnum x, owf_dnum y)
{
    owf_dshape *s = b->s;
    owf_dnum px, py;
    if (b->full || !s->nparts || s->npoints == OWF_DSHAPE_POINTS) {
        b->full = 1;
        return;
    }
    switch (b->turn) {
    case 1: px = b->h - y; py = x; break;             /* south: east turns to point down */
    case 2: px = b->w - x; py = b->h - y; break;      /* west */
    case 3: px = y; py = b->w - x; break;             /* north */
    default: px = x; py = y; break;
    }
    if (b->flip_h) px = b->bw - px;
    if (b->flip_v) py = b->bh - py;
    s->points[s->npoints].x = b->bx + px;
    s->points[s->npoints].y = b->by + py;
    s->npoints++;
    s->parts[s->nparts - 1].n++;
}

/* An arc of the ellipse round cx, cy from angle a0 to a1 (64ths of a turn, either way), a point each step. */
static void arc(build *b, owf_dnum cx, owf_dnum cy, owf_dnum rx, owf_dnum ry, int a0, int a1, int step)
{
    int a = a0, dir = a1 >= a0 ? step : -step;
    for (;;) {
        pt(b, cx + (owf_dnum)((wide)rx * cos64(a) / 10000), cy + (owf_dnum)((wide)ry * sin64(a) / 10000));
        if (a == a1)
            break;
        a += dir;
        if ((dir > 0 && a > a1) || (dir < 0 && a < a1))
            a = a1;
    }
}

static owf_dnum clamp(owf_dnum v, owf_dnum lo, owf_dnum hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

/* A style number: page pixels (hundredths) as given, or def. */
static owf_dnum num(const char *style, const char *key, owf_dnum def)
{
    char v[24];
    return owf_dstyle_get(style, key, v, sizeof v) && v[0] ? owf_dnum_parse(v) : def;
}

/* A part of a length: frac in hundredths (30 is 0.3) of len. */
static owf_dnum part(owf_dnum len, owf_dnum frac)
{
    return (owf_dnum)((wide)len * frac / 100);
}

/* A shape's size key as a length along len: with fixedSize=1 the size is page pixels (px when not given), else a
 * part of len in hundredths (frac when not given), as draw.io's files hold it. */
static owf_dnum size_along(const char *style, owf_dnum len, owf_dnum frac, owf_dnum px)
{
    char v[8];
    if (owf_dstyle_get(style, "fixedSize", v, sizeof v) && !strcmp(v, "1"))
        return num(style, "size", px);
    return part(len, num(style, "size", frac));
}

static owf_dnum smaller(owf_dnum a, owf_dnum b)
{
    return a < b ? a : b;
}

#define BOTH (OWF_DSHAPE_CLOSED | OWF_DSHAPE_FILL | OWF_DSHAPE_LINE)

/* A page with a wavy bottom (the document's): x, y its corner, w by h, the wave a high. */
static void document_at(build *b, owf_dnum x, owf_dnum y, owf_dnum w, owf_dnum h, owf_dnum a)
{
    int k;
    start(b, BOTH);
    pt(b, x, y);
    pt(b, x + w, y);
    for (k = 0; k <= 32; k += 2)         /* one wave, right to left: up past the middle, then down */
        pt(b, x + w - (owf_dnum)((wide)w * k / 32), y + h - a - (owf_dnum)((wide)a * sin64(k * 2) / 10000));
}

static void make(build *b, const char *style, int kind)
{
    owf_dnum w = b->w, h = b->h, d, k, a;
    int i;
    switch (kind) {
    case S_CYLINDER:                     /* the side and bottom, then the top's near edge over it */
        a = clamp(num(style, "size", OWF_DNUM(15)), 0, h / 2);
        start(b, BOTH);
        arc(b, w / 2, a, w / 2, a, 32, 64, 2);
        arc(b, w / 2, h - a, w / 2, a, 0, 32, 2);
        start(b, OWF_DSHAPE_LINE);
        arc(b, w / 2, a, w / 2, a, 32, 0, 2);
        break;
    case S_CLOUD:                        /* round the middle with eight bumps */
        start(b, BOTH);
        for (i = 0; i < 64; i++) {
            int bump = sin64(i * 4);
            wide f = 8500 + 1500 * (wide)(bump < 0 ? -bump : bump) / 10000;
            pt(b, w / 2 + (owf_dnum)((wide)w / 2 * f / 10000 * cos64(i) / 10000),
               h / 2 + (owf_dnum)((wide)h / 2 * f / 10000 * sin64(i) / 10000));
        }
        break;
    case S_DOCUMENT:
        document_at(b, 0, 0, w, h, part(h, clamp(num(style, "size", 30), 0, 100)) / 2);
        break;
    case S_MULTIDOC:                     /* three pages, the front one lowest and leftmost */
        d = smaller(w, h) / 10;
        for (i = 2; i >= 0; i--)
            document_at(b, d * i, d * (2 - i), w - 2 * d, h - 2 * d, (h - 2 * d) * 15 / 100);
        break;
    case S_ACTOR:                        /* a figure: head, body, arms, legs */
        start(b, BOTH);
        arc(b, w / 2, h / 8, w / 4, h / 8, 0, 64, 4);
        start(b, OWF_DSHAPE_LINE);
        pt(b, w / 2, h / 4);
        pt(b, w / 2, h * 2 / 3);
        start(b, OWF_DSHAPE_LINE);
        pt(b, 0, h / 3);
        pt(b, w, h / 3);
        start(b, OWF_DSHAPE_LINE);
        pt(b, 0, h);
        pt(b, w / 2, h * 2 / 3);
        pt(b, w, h);
        break;
    case S_NOTE:                         /* the top right corner folded down */
        d = clamp(num(style, "size", OWF_DNUM(20)), 0, smaller(w, h));
        start(b, BOTH);
        pt(b, 0, 0); pt(b, w - d, 0); pt(b, w, d); pt(b, w, h); pt(b, 0, h);
        start(b, OWF_DSHAPE_LINE);
        pt(b, w - d, 0); pt(b, w - d, d); pt(b, w, d);
        break;
    case S_CARD:                         /* the top left corner cut */
        d = clamp(num(style, "size", OWF_DNUM(20)), 0, smaller(w, h));
        start(b, BOTH);
        pt(b, d, 0); pt(b, w, 0); pt(b, w, h); pt(b, 0, h); pt(b, 0, d);
        break;
    case S_LOOPLIMIT:                    /* both top corners cut */
        d = clamp(num(style, "size", OWF_DNUM(20)), 0, smaller(w / 2, h));
        start(b, BOTH);
        pt(b, d, 0); pt(b, w - d, 0); pt(b, w, d); pt(b, w, h); pt(b, 0, h); pt(b, 0, d);
        break;
    case S_CUBE:                         /* the front, its top and its right side */
        d = clamp(num(style, "size", OWF_DNUM(20)), 0, smaller(w, h));
        start(b, BOTH);
        pt(b, 0, d); pt(b, w - d, d); pt(b, w - d, h); pt(b, 0, h);
        start(b, BOTH);
        pt(b, 0, d); pt(b, d, 0); pt(b, w, 0); pt(b, w - d, d);
        start(b, BOTH);
        pt(b, w - d, d); pt(b, w, 0); pt(b, w, h - d); pt(b, w - d, h);
        break;
    case S_STEP:                         /* a chevron pointing east */
        k = clamp(size_along(style, w, 20, OWF_DNUM(20)), 0, w / 2);
        start(b, BOTH);
        pt(b, 0, 0); pt(b, w - k, 0); pt(b, w, h / 2); pt(b, w - k, h); pt(b, 0, h); pt(b, k, h / 2);
        break;
    case S_TRAPEZOID:                    /* narrower at the top */
        k = clamp(size_along(style, w, 20, OWF_DNUM(20)), 0, w / 2);
        start(b, BOTH);
        pt(b, 0, h); pt(b, k, 0); pt(b, w - k, 0); pt(b, w, h);
        break;
    case S_TAPE:                         /* a wave along the top and the bottom */
        a = part(h, clamp(num(style, "size", 40), 0, 100)) / 4;
        start(b, BOTH);
        for (i = 0; i <= 32; i += 2)
            pt(b, (owf_dnum)((wide)w * i / 32), a - (owf_dnum)((wide)a * sin64(i * 2) / 10000));
        for (i = 0; i <= 32; i += 2)
            pt(b, w - (owf_dnum)((wide)w * i / 32), h - a + (owf_dnum)((wide)a * sin64(i * 2) / 10000));
        break;
    case S_INTERNAL:                     /* a box with a line down near the left and across near the top */
        d = clamp(num(style, "dx", OWF_DNUM(10)), 0, w);
        k = clamp(num(style, "dy", OWF_DNUM(10)), 0, h);
        start(b, BOTH);
        pt(b, 0, 0); pt(b, w, 0); pt(b, w, h); pt(b, 0, h);
        start(b, OWF_DSHAPE_LINE);
        pt(b, d, 0); pt(b, d, h);
        start(b, OWF_DSHAPE_LINE);
        pt(b, 0, k); pt(b, w, k);
        break;
    case S_CALLOUT: {                    /* a box over a pointer down to its tip */
        owf_dnum base = clamp(num(style, "base", OWF_DNUM(20)), 0, w), x1, tip;
        d = clamp(num(style, "size", OWF_DNUM(20)), 0, h);
        x1 = clamp(part(w, num(style, "position", 50)), 0, w - base);
        tip = clamp(part(w, num(style, "position2", 50)), 0, w);
        start(b, BOTH);
        pt(b, 0, 0); pt(b, w, 0); pt(b, w, h - d); pt(b, x1 + base, h - d); pt(b, tip, h); pt(b, x1, h - d);
        pt(b, 0, h - d);
        break;
    }
    case S_DATASTORAGE:                  /* bowed out on the left, in on the right */
        k = clamp(size_along(style, w, 10, OWF_DNUM(10)), 0, w / 2);
        start(b, BOTH);
        pt(b, k, 0);
        arc(b, w, h / 2, k, h / 2, 48, 16, 2);
        arc(b, k, h / 2, k, h / 2, 16, 48, 2);
        break;
    case S_MANUALINPUT:                  /* the top sloping up to the right */
        d = clamp(num(style, "size", OWF_DNUM(30)), 0, h);
        start(b, BOTH);
        pt(b, 0, d); pt(b, w, 0); pt(b, w, h); pt(b, 0, h);
        break;
    case S_DELAY:                        /* square on the left, round on the right */
        a = smaller(w, h / 2);
        start(b, BOTH);
        pt(b, 0, 0);
        arc(b, w - a, h / 2, a, h / 2, 48, 80, 2);
        pt(b, 0, h);
        break;
    case S_DISPLAY:                      /* a point on the left, round on the right */
        k = w / 4;
        a = smaller(w / 4, h / 2);
        start(b, BOTH);
        pt(b, 0, h / 2);
        pt(b, k, 0);
        arc(b, w - a, h / 2, a, h / 2, 48, 80, 2);
        pt(b, k, h);
        break;
    case S_OFFPAGE:                      /* a box with a point at the bottom */
        k = clamp(part(h, num(style, "size", 38)), 0, h);
        start(b, BOTH);
        pt(b, 0, 0); pt(b, w, 0); pt(b, w, h - k); pt(b, w / 2, h); pt(b, 0, h - k);
        break;
    case S_CROSS: {                      /* a plus, its arms as thick as size of the shorter side, twice over */
        owf_dnum t = clamp(part(smaller(w, h), 2 * num(style, "size", 20)), 0, smaller(w, h));
        owf_dnum x0 = (w - t) / 2, y0 = (h - t) / 2;
        start(b, BOTH);
        pt(b, x0, 0); pt(b, x0 + t, 0); pt(b, x0 + t, y0); pt(b, w, y0); pt(b, w, y0 + t); pt(b, x0 + t, y0 + t);
        pt(b, x0 + t, h); pt(b, x0, h); pt(b, x0, y0 + t); pt(b, 0, y0 + t); pt(b, 0, y0); pt(b, x0, y0);
        break;
    }
    case S_SUM:                          /* a circle with a cross through it */
    case S_OR:                           /* and with a plus */
        start(b, BOTH);
        arc(b, w / 2, h / 2, w / 2, h / 2, 0, 64, 2);
        for (i = 0; i < 2; i++) {
            int at = (kind == S_SUM ? 8 : 0) + i * 16;
            start(b, OWF_DSHAPE_LINE);
            pt(b, w / 2 + (owf_dnum)((wide)w / 2 * cos64(at) / 10000), h / 2 + (owf_dnum)((wide)h / 2 * sin64(at) / 10000));
            pt(b, w / 2 - (owf_dnum)((wide)w / 2 * cos64(at) / 10000), h / 2 - (owf_dnum)((wide)h / 2 * sin64(at) / 10000));
        }
        break;
    case S_SORT:                         /* a diamond cut across the middle */
        start(b, BOTH);
        pt(b, w / 2, 0); pt(b, w, h / 2); pt(b, w / 2, h); pt(b, 0, h / 2);
        start(b, OWF_DSHAPE_LINE);
        pt(b, 0, h / 2); pt(b, w, h / 2);
        break;
    case S_COLLATE:                      /* two triangles point to point */
        start(b, BOTH);
        pt(b, 0, 0); pt(b, w, 0); pt(b, w / 2, h / 2);
        start(b, BOTH);
        pt(b, w / 2, h / 2); pt(b, w, h); pt(b, 0, h);
        break;
    case S_TRIANGLE:
        start(b, BOTH);
        pt(b, 0, 0); pt(b, w, h / 2); pt(b, 0, h);
        break;
    case S_ARROW:                        /* a shaft and a head pointing east */
    case S_DOUBLEARROW: {
        owf_dnum shaft = part(h, clamp(num(style, "arrowWidth", 30), 0, 100));
        owf_dnum head = clamp(part(w, num(style, "arrowSize", 20)), 0, kind == S_ARROW ? w : w / 2);
        owf_dnum y1 = (h - shaft) / 2, y2 = y1 + shaft, tail = kind == S_ARROW ? 0 : head;
        start(b, BOTH);
        if (kind == S_ARROW) {
            pt(b, 0, y1);
        } else {
            pt(b, 0, h / 2); pt(b, tail, 0); pt(b, tail, y1);
        }
        pt(b, w - head, y1); pt(b, w - head, 0); pt(b, w, h / 2); pt(b, w - head, h); pt(b, w - head, y2);
        if (kind == S_ARROW) {
            pt(b, 0, y2);
        } else {
            pt(b, tail, y2); pt(b, tail, h);
        }
        break;
    }
    default:
        break;
    }
}

int owf_dshape_outline(const char *style, const owf_dnum box[4], owf_dshape *s)
{
    build b;
    char v[16];
    int turned, kind = kind_of(style, &turned);
    memset(s, 0, sizeof *s);
    if (kind == S_NONE || box[2] <= 0 || box[3] <= 0)
        return 0;
    memset(&b, 0, sizeof b);
    b.s = s;
    b.bx = box[0];
    b.by = box[1];
    b.bw = box[2];
    b.bh = box[3];
    if (owf_dstyle_get(style, "direction", v, sizeof v)) {
        if (!strcmp(v, "south")) b.turn = 1;
        else if (!strcmp(v, "west")) b.turn = 2;
        else if (!strcmp(v, "north")) b.turn = 3;
    }
    b.flip_h = owf_dstyle_get(style, "flipH", v, sizeof v) && !strcmp(v, "1");
    b.flip_v = owf_dstyle_get(style, "flipV", v, sizeof v) && !strcmp(v, "1");
    b.w = b.turn % 2 ? box[3] : box[2];  /* built pointing east, then turned to fill the box */
    b.h = b.turn % 2 ? box[2] : box[3];
    make(&b, style, kind);
    return 1;
}
