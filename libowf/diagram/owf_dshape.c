/*
 * owf_dshape.c: the outlines of GFOpenDiag's shapes beyond the first flowchart set (owf_diagram.h, "Shapes"),
 * worked out once for the canvas and every writer (SVG, PDF, EPS) to draw: closed outlines to fill and stroke,
 * and lines to stroke. The basic and flowchart sets, UML's, and the network set as flat drawings of our own. Integer
 * arithmetic only. The geometry (the pen that turns and flips a shape into its box, arcs, boxes, lines, ovals and
 * the 64-step sine) is GFOffice's one copy in libowf/src/owf_geo.c; what is here is which shape a style names and
 * how each one is drawn.
 * The shapes are drawn in our own way from their plain geometric descriptions; only their names (draw.io's
 * style words, "shape=cylinder3", "shape=note", and its direction, flipH and flipV keys) are taken from the
 * .drawio format, so that a file made here opens in diagrams.net as the same shapes and one made there opens
 * here. No other program's code.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "owf_diag_int.h"
#include "owf_geo.h"

#include <string.h>

typedef long long wide;

/* the curves' sine, owf_geo.c's: sin and cos of a/64 of a turn, times 10000 */
#define sin64(a) owf_geo_sin64(a)
#define cos64(a) owf_geo_cos64(a)

/* ---- The table of shapes ---- */

enum {
    S_NONE, S_CYLINDER, S_CLOUD, S_DOCUMENT, S_MULTIDOC, S_ACTOR, S_NOTE, S_CARD, S_CUBE, S_STEP, S_TRAPEZOID,
    S_TAPE, S_INTERNAL, S_CALLOUT, S_DATASTORAGE, S_MANUALINPUT, S_DELAY, S_DISPLAY, S_OFFPAGE, S_CROSS, S_SUM,
    S_OR, S_LOOPLIMIT, S_SORT, S_COLLATE, S_TRIANGLE, S_ARROW, S_DOUBLEARROW,
    /* UML */
    S_LIFELINE, S_BOUNDARY, S_ENTITY, S_CONTROL, S_DESTROY, S_COMPONENT, S_FOLDER, S_FRAME, S_REQUIRED, S_PROVREQ,
    S_ENDSTATE, S_LOLLIPOP,
    /* network */
    S_SERVER, S_PC, S_MONITOR, S_LAPTOP, S_ROUTER, S_SWITCH, S_HUB, S_FIREWALL, S_PRINTER, S_MOBILE, S_TABLET,
    S_MODEM, S_STORAGE, S_MAINFRAME, S_RACK, S_PATCHPANEL, S_USER
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
    { "umlLifeline", S_LIFELINE }, { "umlBoundary", S_BOUNDARY }, { "umlEntity", S_ENTITY },
    { "umlControl", S_CONTROL }, { "umlDestroy", S_DESTROY }, { "component", S_COMPONENT }, { "module", S_COMPONENT },
    { "folder", S_FOLDER }, { "umlFrame", S_FRAME }, { "requiredInterface", S_REQUIRED },
    { "providedRequiredInterface", S_PROVREQ }, { "endState", S_ENDSTATE }, { "lollipop", S_LOLLIPOP },
    { "mxgraph.networks.server", S_SERVER }, { "mxgraph.networks.pc", S_PC },
    { "mxgraph.networks.monitor", S_MONITOR }, { "mxgraph.networks.laptop", S_LAPTOP },
    { "mxgraph.networks.router", S_ROUTER }, { "mxgraph.networks.switch", S_SWITCH },
    { "mxgraph.networks.hub", S_HUB }, { "mxgraph.networks.firewall", S_FIREWALL },
    { "mxgraph.networks.printer", S_PRINTER }, { "mxgraph.networks.mobile", S_MOBILE },
    { "mxgraph.networks.tablet", S_TABLET }, { "mxgraph.networks.modem", S_MODEM },
    { "mxgraph.networks.storage", S_STORAGE }, { "mxgraph.networks.mainframe", S_MAINFRAME },
    { "mxgraph.networks.rack", S_RACK }, { "mxgraph.networks.patch_panel", S_PATCHPANEL },
    { "mxgraph.networks.user_male", S_USER }, { "mxgraph.networks.cloud", S_CLOUD },
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

/* A shape is built with owf_geo.c's pen: pointing east, w across and h down, then turned and flipped into the box
 * (owf_geo_pen: s, bx, by, bw, bh, w, h, turn, flip_h, flip_v, full). */
typedef owf_geo_pen build;

#define start(b, flags) owf_geo_start(b, flags)
#define pt(b, x, y) owf_geo_point(b, x, y)
#define arc(b, cx, cy, rx, ry, a0, a1, step) owf_geo_arc(b, cx, cy, rx, ry, a0, a1, step)

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
#define INKED (OWF_DSHAPE_CLOSED | OWF_DSHAPE_INK | OWF_DSHAPE_LINE)

/* A box from x0, y0 to x1, y1 as one part; a line as one; a whole ellipse as one, a point each step 64ths of a
 * turn (owf_geo.c). */
#define rect(b, flags, x0, y0, x1, y1) owf_geo_rect(b, flags, x0, y0, x1, y1)
#define line(b, flags, x0, y0, x1, y1) owf_geo_line(b, flags, x0, y0, x1, y1)
#define oval(b, flags, cx, cy, rx, ry, step) owf_geo_oval(b, flags, cx, cy, rx, ry, step)

/* A short drum from y to y + h (the cylinder's, its ends a high): the side and bottom, then the top's near edge. */
static void drum(build *b, owf_dnum y, owf_dnum w, owf_dnum h, owf_dnum a, int step)
{
    start(b, BOTH);
    arc(b, w / 2, y + a, w / 2, a, 32, 64, step);
    arc(b, w / 2, y + h - a, w / 2, a, 0, 32, step);
    start(b, OWF_DSHAPE_LINE);
    arc(b, w / 2, y + a, w / 2, a, 32, 0, step);
}

/* An arrow as one line: from x0, y0 to its point at x1, y1 (level), the head's two barbs d long. */
static void arrow_line(build *b, owf_dnum x0, owf_dnum x1, owf_dnum y, owf_dnum d)
{
    owf_dnum back = x1 > x0 ? -d : d;
    start(b, OWF_DSHAPE_LINE);
    pt(b, x0, y); pt(b, x1, y); pt(b, x1 + back, y - d); pt(b, x1, y); pt(b, x1 + back, y + d);
}

/* A slab seen from above and to the right: its front, top and right side, the top d deep (the switch's and hub's). */
static void slab(build *b, owf_dnum w, owf_dnum h, owf_dnum d)
{
    start(b, BOTH);
    pt(b, 0, d); pt(b, w - d, d); pt(b, w - d, h); pt(b, 0, h);
    start(b, BOTH);
    pt(b, 0, d); pt(b, d, 0); pt(b, w, 0); pt(b, w - d, d);
    start(b, BOTH);
    pt(b, w - d, d); pt(b, w, 0); pt(b, w, h - d); pt(b, w - d, h);
}

/* A hand-held: its body, its screen inside m across and t from the top to f from the bottom, and a button. */
static void handheld(build *b, owf_dnum w, owf_dnum h, owf_dnum m, owf_dnum t, owf_dnum f)
{
    owf_dnum r = smaller(w, f) / 4;
    rect(b, BOTH, 0, 0, w, h);
    rect(b, OWF_DSHAPE_CLOSED | OWF_DSHAPE_LINE, m, t, w - m, h - f);
    oval(b, INKED, w / 2, h - f / 2, r, r, 8);
}

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
    /* ---- UML ---- */
    case S_LIFELINE:                     /* a box at the top, a dashed line down from it */
        d = clamp(num(style, "size", OWF_DNUM(40)), 0, h);
        rect(b, BOTH, 0, 0, w, d);
        line(b, OWF_DSHAPE_DASHED, w / 2, d, w / 2, h);
        break;
    case S_BOUNDARY:                     /* a circle held off a bar on the left */
        oval(b, BOTH, w * 5 / 8, h / 2, w * 3 / 8, h / 2, 2);
        line(b, 0, 0, 0, 0, h);
        line(b, 0, 0, h / 2, w / 4, h / 2);
        break;
    case S_ENTITY:                       /* a circle on a line */
        oval(b, BOTH, w / 2, h * 15 / 32, w / 2, h * 15 / 32, 2);
        line(b, 0, 0, h, w, h);
        break;
    case S_CONTROL:                      /* a circle with an arrowhead on its top */
        d = h / 8;
        oval(b, BOTH, w / 2, (h + d) / 2, w / 2, (h - d) / 2, 2);
        start(b, OWF_DSHAPE_LINE);
        pt(b, w / 2 + w / 8, 0); pt(b, w / 2, d); pt(b, w / 2 + w / 8, 2 * d);
        break;
    case S_DESTROY:                      /* a cross */
        line(b, 0, 0, 0, w, h);
        line(b, 0, w, 0, 0, h);
        break;
    case S_COMPONENT: {                  /* a box with two tabs across its left side */
        owf_dnum jw = clamp(num(style, "jettyWidth", OWF_DNUM(20)), 0, w / 2);
        owf_dnum jh = clamp(num(style, "jettyHeight", OWF_DNUM(10)), 0, h / 4);
        rect(b, BOTH, jw / 2, 0, w, h);
        rect(b, BOTH, 0, h * 3 / 10, jw, h * 3 / 10 + jh);
        rect(b, BOTH, 0, h * 6 / 10, jw, h * 6 / 10 + jh);
        break;
    }
    case S_FOLDER: {                     /* a box with a tab on its top left (a UML package) */
        owf_dnum tw = clamp(num(style, "tabWidth", OWF_DNUM(50)), 0, w);
        owf_dnum th = clamp(num(style, "tabHeight", OWF_DNUM(20)), 0, h);
        start(b, BOTH);
        pt(b, 0, 0); pt(b, tw, 0); pt(b, tw, th); pt(b, w, th); pt(b, w, h); pt(b, 0, h);
        line(b, 0, 0, th, tw, th);
        break;
    }
    case S_FRAME: {                      /* a box with its name's corner cut off at the top left */
        owf_dnum fw = clamp(num(style, "width", OWF_DNUM(60)), 0, w);
        owf_dnum fh = clamp(num(style, "height", OWF_DNUM(30)), 0, h);
        d = smaller(fh / 2, fw);
        rect(b, BOTH, 0, 0, w, h);
        start(b, OWF_DSHAPE_LINE);
        pt(b, 0, fh); pt(b, fw - d, fh); pt(b, fw, fh - d); pt(b, fw, 0);
        break;
    }
    case S_REQUIRED:                     /* a socket: half a circle open to the left */
        start(b, OWF_DSHAPE_LINE);
        arc(b, 0, h / 2, w, h / 2, 48, 80, 2);
        break;
    case S_PROVREQ:                      /* a ball in a socket */
        oval(b, BOTH, w / 4, h / 2, w / 4, h / 4, 2);
        start(b, OWF_DSHAPE_LINE);
        arc(b, w * 2 / 5, h / 2, w * 3 / 5, h / 2, 48, 80, 2);
        break;
    case S_ENDSTATE:                     /* a ring round a dot */
        oval(b, BOTH, w / 2, h / 2, w / 2, h / 2, 2);
        oval(b, INKED, w / 2, h / 2, w * 3 / 10, h * 3 / 10, 2);
        break;
    case S_LOLLIPOP:                     /* a ball on a stick from the left */
        a = smaller(w / 2, h / 2);
        line(b, 0, 0, h / 2, w - 2 * a, h / 2);
        oval(b, BOTH, w - a, h / 2, a, h / 2, 2);
        break;
    /* ---- Network: flat drawings of our own ---- */
    case S_SERVER:                       /* a tower with two drive slots and a button */
        rect(b, BOTH, 0, 0, w, h);
        line(b, 0, w / 5, h / 6, w * 4 / 5, h / 6);
        line(b, 0, w / 5, h / 3, w * 4 / 5, h / 3);
        a = smaller(w, h) / 10;
        oval(b, INKED, w / 2, h * 3 / 4, a, a, 8);
        break;
    case S_PC:                           /* a screen and keyboard on the left, a tower on the right */
        rect(b, BOTH, w * 7 / 10, 0, w, h);
        rect(b, BOTH, 0, h / 20, w * 62 / 100, h * 6 / 10);
        rect(b, OWF_DSHAPE_CLOSED | OWF_DSHAPE_LINE, w / 20, h * 3 / 20, w * 57 / 100, h * 5 / 10);
        rect(b, BOTH, w * 26 / 100, h * 6 / 10, w * 36 / 100, h * 3 / 4);
        rect(b, BOTH, 0, h * 85 / 100, w * 62 / 100, h);
        line(b, 0, w * 75 / 100, h / 5, w * 95 / 100, h / 5);
        break;
    case S_MONITOR:                      /* a screen on a stand */
        rect(b, BOTH, 0, 0, w, h * 7 / 10);
        rect(b, OWF_DSHAPE_CLOSED | OWF_DSHAPE_LINE, w / 12, h / 12, w - w / 12, h * 62 / 100);
        start(b, BOTH);
        pt(b, w * 2 / 5, h * 7 / 10); pt(b, w * 3 / 5, h * 7 / 10); pt(b, w * 65 / 100, h * 85 / 100);
        pt(b, w * 35 / 100, h * 85 / 100);
        rect(b, BOTH, w / 5, h * 85 / 100, w * 4 / 5, h);
        break;
    case S_LAPTOP:                       /* a lid and its screen over a base seen from the front */
        rect(b, BOTH, w / 10, 0, w * 9 / 10, h * 3 / 4);
        rect(b, OWF_DSHAPE_CLOSED | OWF_DSHAPE_LINE, w * 3 / 20, h / 15, w * 17 / 20, h * 2 / 3);
        start(b, BOTH);
        pt(b, w / 10, h * 3 / 4); pt(b, w * 9 / 10, h * 3 / 4); pt(b, w, h); pt(b, 0, h);
        break;
    case S_ROUTER:                       /* a puck with two crossed arrows on its top */
        a = h / 4;
        drum(b, 0, w, h, a, 2);
        arrow_line(b, w * 3 / 10, w * 7 / 10, a * 2 / 3, a / 3);
        arrow_line(b, w * 7 / 10, w * 3 / 10, a * 4 / 3, a / 3);
        break;
    case S_SWITCH:                       /* a slab with two arrows on its top, one each way */
        d = h / 3;
        slab(b, w, h, d);
        arrow_line(b, w / 4, w * 3 / 4, d / 3, d / 4);
        arrow_line(b, w * 3 / 4, w / 4, d * 2 / 3, d / 4);
        break;
    case S_HUB:                          /* a slab with a row of ports along its front */
        d = h / 3;
        slab(b, w, h, d);
        start(b, OWF_DSHAPE_LINE);
        for (i = 0; i < 6; i++) {        /* the ports as a battlement */
            owf_dnum x = (w - d) * (2 * i + 1) / 13;
            pt(b, x, h * 3 / 4); pt(b, x, h * 3 / 5); pt(b, x + (w - d) / 13, h * 3 / 5);
            pt(b, x + (w - d) / 13, h * 3 / 4);
        }
        break;
    case S_FIREWALL:                     /* a brick wall: three courses, the joints between bricks staggered */
        rect(b, BOTH, 0, 0, w, h);
        line(b, 0, 0, h / 3, w, h / 3);
        line(b, 0, 0, h * 2 / 3, w, h * 2 / 3);
        line(b, 0, w / 2, 0, w / 2, h / 3);
        line(b, 0, w / 4, h / 3, w / 4, h * 2 / 3);
        line(b, 0, w * 3 / 4, h / 3, w * 3 / 4, h * 2 / 3);
        line(b, 0, w / 2, h * 2 / 3, w / 2, h);
        break;
    case S_PRINTER:                      /* paper in at the top, a body, a tray out at the bottom */
        rect(b, BOTH, w / 5, 0, w * 4 / 5, h * 2 / 5);
        rect(b, BOTH, w * 3 / 20, h * 7 / 10, w * 17 / 20, h);
        rect(b, BOTH, 0, h * 3 / 10, w, h * 3 / 4);
        line(b, 0, w / 5, h * 3 / 5, w * 4 / 5, h * 3 / 5);
        break;
    case S_MOBILE:
        handheld(b, w, h, w / 10, h / 8, h / 6);
        break;
    case S_TABLET:
        handheld(b, w, h, w / 16, h / 12, h / 10);
        break;
    case S_MODEM:                        /* a low box with an aerial and three lights */
        rect(b, BOTH, 0, h / 3, w, h);
        line(b, 0, w * 4 / 5, h / 3, w * 9 / 10, 0);
        a = smaller(w / 16, h / 10);
        for (i = 0; i < 3; i++)
            oval(b, INKED, w * (2 + 2 * i) / 10, h * 2 / 3, a, a, 8);
        break;
    case S_STORAGE:                      /* three disks stacked */
        k = h / 3;
        a = k / 4;
        for (i = 2; i >= 0; i--)
            drum(b, k * i, w, i == 2 ? h - 2 * k : k, a, 4);
        break;
    case S_MAINFRAME:                    /* a cabinet with a panel along its top and three doors */
        rect(b, BOTH, 0, 0, w, h);
        line(b, 0, 0, h / 6, w, h / 6);
        line(b, 0, w / 3, h / 6, w / 3, h);
        line(b, 0, w * 2 / 3, h / 6, w * 2 / 3, h);
        break;
    case S_RACK:                         /* a frame with units across it */
        d = smaller(w, h) / 12;
        rect(b, BOTH, 0, 0, w, h);
        rect(b, OWF_DSHAPE_CLOSED | OWF_DSHAPE_LINE, d, d, w - d, h - d);
        for (i = 1; i <= 4; i++)
            line(b, 0, d, d + (h - 2 * d) * i / 5, w - d, d + (h - 2 * d) * i / 5);
        break;
    case S_PATCHPANEL:                   /* a long strip with a row of sockets */
        rect(b, BOTH, 0, 0, w, h);
        for (i = 0; i < 6; i++) {
            owf_dnum x = w * (2 * i + 1) / 13;
            rect(b, INKED, x, h / 3, x + w / 13, h * 2 / 3);
        }
        break;
    case S_USER:                         /* a head over shoulders */
        oval(b, BOTH, w / 2, h / 4, w / 4, h / 4, 2);
        start(b, BOTH);
        arc(b, w / 2, h, w / 2, h / 2 - h / 20, 32, 64, 2);
        break;
    default:
        break;
    }
}

int owf_dshape_outline(const char *style, const owf_dnum box[4], owf_dshape *s)
{
    build b;
    char v[16];
    int turned, kind = kind_of(style, &turned), turn = 0;
    memset(s, 0, sizeof *s);
    if (kind == S_NONE || box[2] <= 0 || box[3] <= 0)
        return 0;
    if (owf_dstyle_get(style, "direction", v, sizeof v)) {
        if (!strcmp(v, "south")) turn = 1;
        else if (!strcmp(v, "west")) turn = 2;
        else if (!strcmp(v, "north")) turn = 3;
    }
    owf_geo_pen_init(&b, s, box, turn, owf_dstyle_get(style, "flipH", v, sizeof v) && !strcmp(v, "1"),
                       owf_dstyle_get(style, "flipV", v, sizeof v) && !strcmp(v, "1"));
    make(&b, style, kind);
    return 1;
}
