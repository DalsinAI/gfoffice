/*
 * GFOpenDiag's canvas on the PC (src/diag/odg_view.c, opendiag_view.h): the view's places, the shapes, connectors,
 * labels, grid, selection and handles, drawn onto a host target of 0xRRGGBB pixels through GFOpenLayout's drawing
 * calls. Text is a block font: 6 pixels a byte, 7 up and 2 down. Every pixel below was worked out by hand. Run under
 * ASan and UBSan by make check.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <stdio.h>
#include <string.h>
#include "opendiag_view.h"

static int fails, checks;

#define CHECK(cond) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_N(got, want) do { long g_ = (long)(got), w_ = (long)(want); checks++; if (g_ != w_) { fails++; \
    printf("FAIL %s:%d: %s is %ld, want %ld\n", __FILE__, __LINE__, #got, g_, w_); } } while (0)
#define CHECK_X(got, want) do { unsigned long g_ = (unsigned long)(got), w_ = (unsigned long)(want); checks++; \
    if (g_ != w_) { fails++; printf("FAIL %s:%d: %s is %06lX, want %06lX\n", __FILE__, __LINE__, #got, g_, w_); } } while (0)
#define PX(n) OWF_DNUM(n)

/* ---- a host target ---- */
#define W 400
#define H 300
static unsigned long px[H][W];
static oldraw *cur;
typedef struct { char s[32]; int x, y, size; unsigned style; } text_rec;
static text_rec texts[32];
static int ntexts;

static void t_fill(void *t, int x, int y, int w, int h, oldraw_colour c)
{
    int i, j;
    unsigned long v = oldraw_resolve(cur, c) & 0xFFFFFFUL;
    (void)t;
    for (j = y; j < y + h; j++)
        for (i = x; i < x + w; i++) {
            if (i < 0 || j < 0 || i >= W || j >= H) {
                fails++;
                printf("FAIL fill outside the target at %d,%d\n", i, j);
                return;
            }
            px[j][i] = v;
        }
}
static void t_invert(void *t, int x, int y, int w, int h) { (void)t; (void)x; (void)y; (void)w; (void)h; }
static void t_image(void *t, const oldraw_image *img, int sx, int sy, int x, int y, int w, int h, unsigned flags)
{
    (void)t; (void)img; (void)sx; (void)sy; (void)x; (void)y; (void)w; (void)h; (void)flags;
}
static int t_measure(void *t, const char *s, size_t n, const oldraw_font *f, oldraw_text_size *size)
{
    (void)t; (void)s; (void)f;
    size->width = (int)n * 6;
    size->ascent = 7;
    size->descent = 2;
    return 1;
}
static int t_text(void *t, int x, int y, const char *s, size_t n, const oldraw_font *f, oldraw_colour c, unsigned st,
                  const oldraw_rect *clip)
{
    (void)t; (void)c; (void)clip;
    if (ntexts < 32) {
        size_t k = n < 31 ? n : 31;
        memcpy(texts[ntexts].s, s, k);
        texts[ntexts].s[k] = 0;
        texts[ntexts].x = x;
        texts[ntexts].y = y;
        texts[ntexts].size = f ? f->px : 0;
        texts[ntexts].style = st;
        ntexts++;
    }
    return 1;
}
static void t_flush(void *t) { (void)t; }
static int t_copy(void *t, int sx, int sy, int dx, int dy, int w, int h)
{
    (void)t; (void)sx; (void)sy; (void)dx; (void)dy; (void)w; (void)h;
    return 1;
}
static const oldraw_ops ops = { t_fill, t_invert, t_image, t_text, t_measure, t_flush, NULL, t_copy };

static const text_rec *drew(const char *s)
{
    int i;
    for (i = 0; i < ntexts; i++)
        if (!strcmp(texts[i].s, s)) return &texts[i];
    return NULL;
}

static int count(int x, int y, int w, int h, unsigned long v)
{
    int i, j, n = 0;
    for (j = y; j < y + h; j++)
        for (i = x; i < x + w; i++)
            if (i >= 0 && j >= 0 && i < W && j < H && px[j][i] == v) n++;
    return n;
}

#define WHITE 0xFFFFFFUL
#define BLACK 0x000000UL
#define RED 0xFF0000UL
#define GREEN 0x00FF00UL
#define BLUE 0x0000FFUL
#define SEL 0x2060E0UL
#define DOT 0xD0D0D0UL

static owf_diagram dg;
static odg_doc doc;
static oldraw *d;

static void fresh(int grid)
{
    owf_dpage *p;
    owf_diagram_init(&dg);
    p = owf_diagram_add_page(&dg, "p", "Page-1");
    if (grid) {
        owf_dattrs_set(&p->model, "grid", "1");
        owf_dattrs_set(&p->model, "gridSize", "10");
    }
    odg_init(&doc, &dg, 0);
}

static void done(void)
{
    odg_free(&doc);
    owf_diagram_free(&dg);
}

static void draw(const odg_view *v)
{
    ntexts = 0;
    odg_draw(d, v, &doc, NULL);
}

static void test_places(void)
{
    odg_view v;
    char id[24];
    fresh(0);
    odg_view_init(&v, W, H);
    CHECK_N(odg_view_x(&v, PX(50)), 50);
    CHECK_N(odg_page_x(&v, 50), PX(50));
    v.zoom = 200;
    CHECK_N(odg_view_x(&v, PX(50)), 100);
    CHECK_N(odg_view_y(&v, PX(-3)), -6);
    CHECK_N(odg_page_y(&v, 101), 5050);
    CHECK_N(odg_view_len(&v, PX(7)), 14);
    v.zoom = 100;
    odg_view_zoom_at(&v, 200, 100, 100);      /* page (100, 100) stays under the pointer */
    CHECK_N(v.left, PX(50));
    CHECK_N(odg_view_x(&v, PX(100)), 100);
    CHECK_N(odg_view_y(&v, PX(100)), 100);
    odg_view_zoom_at(&v, 5, 0, 0);
    CHECK_N(v.zoom, ODG_ZOOM_MIN);
    odg_view_zoom_at(&v, 5000, 0, 0);
    CHECK_N(v.zoom, ODG_ZOOM_MAX);
    /* far off: kept inside what oldraw takes */
    v.zoom = 800;
    v.left = 0;
    CHECK(odg_view_x(&v, 2000000000L) <= (1L << 28));
    /* a view placed at 30, 40 on its target (a window's canvas): everything moves with it */
    v.zoom = 100;
    v.left = v.top = 0;
    v.x = 30;
    v.y = 40;
    CHECK_N(odg_view_x(&v, PX(5)), 35);
    CHECK_N(odg_view_y(&v, PX(5)), 45);
    CHECK_N(odg_page_x(&v, 35), PX(5));
    odg_view_zoom_at(&v, 200, 130, 140);      /* the page point under 130, 140 (100, 100) stays there */
    CHECK_N(v.left, PX(50));
    CHECK_N(odg_page_x(&v, 130), PX(100));
    CHECK_N(odg_page_y(&v, 140), PX(100));
    v.x = v.y = 0;

    /* fitting: nothing drawn is the corner at 100%; a small drawing at 100%, centred; a big one shrunk */
    odg_view_fit(&v, &doc);
    CHECK_N(v.zoom, 100);
    CHECK_N(v.left, 0);
    odg_add_shape(&doc, PX(100), PX(100), PX(200), PX(100), "", NULL, id);
    odg_view_fit(&v, &doc);
    CHECK_N(v.zoom, 100);
    CHECK_N(v.left, 0);
    CHECK_N(v.top, 0);
    odg_add_shape(&doc, 0, 0, PX(2000), PX(1000), "", NULL, id);
    odg_view_fit(&v, &doc);
    CHECK_N(v.zoom, 19);
    CHECK_N(v.left, -5263);
    CHECK(odg_view_x(&v, 0) >= 0 && odg_view_x(&v, PX(2000)) < W);
    CHECK(odg_view_y(&v, 0) >= 0 && odg_view_y(&v, PX(1000)) < H);
    done();
}

static void test_colours(void)
{
    CHECK_X(odg_style_colour("fillColor=#ff0000;", "fillColor", 0), 0xFFFF0000UL);
    CHECK_X(odg_style_colour("fillColor=#0f0;", "fillColor", 0), 0xFF00FF00UL);
    CHECK_X(odg_style_colour("fillColor=#00FF7f;", "fillColor", 0), 0xFF00FF7FUL);
    CHECK_X(odg_style_colour("fillColor=none;", "fillColor", 1), OLDRAW_NONE);
    CHECK_X(odg_style_colour("fillColor=default;", "fillColor", 7), 7);
    CHECK_X(odg_style_colour("fillColor=#12;", "fillColor", 7), 7);
    CHECK_X(odg_style_colour("fillColor=red;", "fillColor", 7), 7);
    CHECK_X(odg_style_colour("fillColor=#gg0000;", "fillColor", 7), 7);
    CHECK_X(odg_style_colour(NULL, "fillColor", 7), 7);
}

static void test_shapes(void)
{
    odg_view v;
    char a[24], b[24], c[24], e[24];
    owf_dpoint p0 = { PX(20), PX(100) }, p1 = { PX(120), PX(100) }, q0 = { PX(20), PX(150) }, q1 = { PX(120), PX(150) };
    fresh(0);
    odg_view_init(&v, W, H);
    odg_add_shape(&doc, PX(10), PX(10), PX(40), PX(20), "fillColor=#ff0000;strokeColor=#0000ff;", NULL, a);
    odg_add_shape(&doc, PX(100), PX(10), PX(40), PX(40), "ellipse;fillColor=#00ff00;", NULL, b);
    odg_add_shape(&doc, PX(200), PX(10), PX(40), PX(40), "rhombus;fillColor=#ff0000;", NULL, c);
    odg_add_edge(&doc, NULL, NULL, &p0, &p1, "", e);
    odg_add_edge(&doc, NULL, NULL, &q0, &q1, "endArrow=none;dashed=1;", e);
    draw(&v);
    /* the box: filled inside, its outline one pixel on its edge pixels, nothing past it */
    CHECK_X(px[20][30], RED);
    CHECK_X(px[10][10], BLUE);
    CHECK_X(px[29][49], BLUE);
    CHECK_X(px[20][10], BLUE);
    CHECK_X(px[11][11], RED);
    CHECK_X(px[30][50], WHITE);
    CHECK_X(px[9][9], WHITE);
    CHECK_N(count(10, 10, 40, 1, BLUE), 40);
    /* the ellipse: its middle filled, its box's corners not, its outline black */
    CHECK_X(px[30][120], GREEN);
    CHECK_X(px[10][100], WHITE);
    CHECK_X(px[49][139], WHITE);
    CHECK_X(px[10][120], BLACK);
    CHECK_X(px[30][100], BLACK);
    /* the rhombus */
    CHECK_X(px[30][220], RED);
    CHECK_X(px[11][201], WHITE);
    CHECK_X(px[10][220], BLACK);
    CHECK_X(px[30][200], BLACK);
    /* a connector: its line, its arrowhead at the end (8 long, 3 each side), none on the other */
    CHECK_X(px[100][60], BLACK);
    CHECK_X(px[101][114], BLACK);
    CHECK_X(px[99][114], BLACK);
    CHECK_X(px[104][114], WHITE);
    CHECK_X(px[101][60], WHITE);
    CHECK_X(px[151][114], WHITE);
    /* dashed: three on, three off */
    CHECK_X(px[150][20], BLACK);
    CHECK_X(px[150][22], BLACK);
    CHECK_X(px[150][24], WHITE);
    CHECK_X(px[150][26], BLACK);
    CHECK(count(20, 150, 101, 1, BLACK) >= 48 && count(20, 150, 101, 1, BLACK) <= 54);

    /* at 200% everything twice the size */
    v.zoom = 200;
    draw(&v);
    CHECK_X(px[40][60], RED);
    CHECK_X(px[59][99], BLUE);
    CHECK_X(px[20][20], BLUE);
    CHECK_X(px[60][100], WHITE);
    CHECK_X(px[19][40], BLUE);                /* the stroke is two pixels wide, centred on the edge */
    CHECK_X(px[21][40], RED);

    /* hidden: not drawn */
    v.zoom = 100;
    owf_dattrs_set(&owf_dpage_find(odg_page(&doc), a)->extra, "visible", "0");
    draw(&v);
    CHECK_X(px[20][30], WHITE);
    CHECK_X(px[10][10], WHITE);
    done();
}

static void test_rounded_and_swimlane(void)
{
    odg_view v;
    char a[24], b[24];
    fresh(0);
    odg_view_init(&v, W, H);
    odg_add_shape(&doc, PX(10), PX(10), PX(100), PX(60), "rounded=1;fillColor=#ff0000;", NULL, a);
    odg_add_shape(&doc, PX(200), PX(10), PX(100), PX(100), "swimlane;", NULL, b);
    draw(&v);
    /* corners cut round (radius 60 * 15% / 2 = 4), sides straight, inside filled */
    CHECK_X(px[10][10], WHITE);
    CHECK_X(px[69][109], WHITE);
    CHECK_X(px[40][10], BLACK);
    CHECK_X(px[10][60], BLACK);
    CHECK_X(px[69][60], BLACK);
    CHECK_X(px[40][60], RED);
    CHECK_X(px[15][15], RED);
    /* a swimlane's header line 23 down */
    CHECK_N(count(200, 33, 100, 1, BLACK), 100);
    CHECK_X(px[50][250], WHITE);
    done();
}

static void test_labels(void)
{
    odg_view v;
    char a[24], e[24];
    const text_rec *t;
    owf_dpoint p0 = { PX(20), PX(200) }, p1 = { PX(120), PX(200) };
    fresh(0);
    odg_view_init(&v, W, H);
    odg_add_shape(&doc, PX(10), PX(10), PX(40), PX(20), "", "Hello", a);
    odg_add_shape(&doc, PX(100), PX(10), PX(100), PX(60), "html=1;align=left;verticalAlign=top;fontStyle=1;",
                  "one<br>two &amp; three", a);
    odg_add_shape(&doc, PX(10), PX(100), PX(40), PX(20), "text;fontSize=20;", "Big", a);
    odg_add_edge(&doc, NULL, NULL, &p0, &p1, "", e);
    odg_set_value(&doc, e, "go");
    draw(&v);
    /* centred: "Hello" is 30 wide in the box's middle (30, 20); one line 14 high, so its top is 13 and its
     * baseline 13 + 1 + 7 */
    t = drew("Hello");
    CHECK(t != NULL);
    if (t) {
        CHECK_N(t->x, 15);
        CHECK_N(t->y, 21);
        CHECK_N(t->size, 12);
    }
    /* left and top, 2 in: two lines 14 apart, bold, the entity read */
    t = drew("one");
    CHECK(t != NULL);
    if (t) {
        CHECK_N(t->x, 102);
        CHECK_N(t->y, 20);
        CHECK(t->style & OLDRAW_BOLD);
    }
    t = drew("two & three");
    CHECK(t != NULL);
    if (t)
        CHECK_N(t->y, 34);
    /* a text shape draws no box; its size is its fontSize */
    t = drew("Big");
    CHECK(t != NULL);
    if (t)
        CHECK_N(t->size, 20);
    CHECK_X(px[100][10], WHITE);
    /* the connector's label at its middle (70, 200), on white */
    t = drew("go");
    CHECK(t != NULL);
    if (t) {
        CHECK_N(t->x, 64);
        CHECK_N(t->y, 201);
    }
    CHECK_X(px[200][70], WHITE);
    CHECK_X(px[200][40], BLACK);
    /* zoomed far out the text is too small to draw */
    v.zoom = 10;
    draw(&v);
    CHECK_N(ntexts, 0);
    /* at 200% twice the size */
    v.zoom = 200;
    draw(&v);
    t = drew("Hello");
    CHECK(t != NULL);
    if (t)
        CHECK_N(t->size, 24);
    done();
}

static void test_grid_and_area(void)
{
    odg_view v;
    int i, j, kept = 1;
    fresh(1);
    odg_view_init(&v, W, H);
    draw(&v);
    CHECK_X(px[0][0], DOT);
    CHECK_X(px[10][20], DOT);
    CHECK_X(px[5][5], WHITE);
    CHECK_N(count(0, 0, 100, 100, DOT), 100);
    v.zoom = 50;                              /* 5 pixels apart: too close, none */
    draw(&v);
    CHECK_N(count(0, 0, W, H, DOT), 0);
    v.zoom = 100;
    v.grid = 0;
    draw(&v);
    CHECK_N(count(0, 0, W, H, DOT), 0);
    /* an area: nothing outside it drawn */
    for (j = 0; j < H; j++)
        for (i = 0; i < W; i++) px[j][i] = 0x123456UL;
    {
        oldraw_rect area = { 40, 40, 20, 10 };
        v.grid = 1;
        odg_draw(d, &v, &doc, &area);
    }
    for (j = 0; j < H; j++)
        for (i = 0; i < W; i++)
            if ((i < 40 || i >= 60 || j < 40 || j >= 50) && px[j][i] != 0x123456UL) kept = 0;
    CHECK(kept);
    CHECK_X(px[40][40], DOT);
    CHECK_X(px[45][45], WHITE);
    /* placed on the target: the paper and the grid start at the view's corner, nothing before it */
    for (j = 0; j < H; j++)
        for (i = 0; i < W; i++) px[j][i] = 0x123456UL;
    v.x = 100;
    v.y = 50;
    v.width = 60;
    v.height = 40;
    v.left = v.top = 0;
    draw(&v);
    CHECK_X(px[50][100], DOT);
    CHECK_X(px[60][110], DOT);
    CHECK_X(px[49][100], 0x123456UL);
    CHECK_X(px[50][99], 0x123456UL);
    CHECK_X(px[90][160], 0x123456UL);
    CHECK_N(count(100, 50, 60, 40, 0x123456UL), 0);
    done();
}

static void test_selection_and_handles(void)
{
    odg_view v;
    char a[24], e[24], id[24];
    const char *sel[2];
    owf_dnum box[4] = { PX(10), PX(10), PX(40), PX(20) }, out[4];
    owf_dpoint p0 = { PX(100), PX(200) }, p1 = { PX(200), PX(200) };
    fresh(1);
    odg_view_init(&v, W, H);
    v.grid = 0;
    odg_add_shape(&doc, PX(10), PX(10), PX(40), PX(20), "", NULL, a);
    odg_add_edge(&doc, NULL, NULL, &p0, &p1, "", e);
    sel[0] = a;
    sel[1] = e;
    v.selected = sel;
    v.nselected = 2;
    draw(&v);
    /* handles: 7 square, outlined in the selection's colour, round each corner and side's middle */
    CHECK_X(px[7][7], SEL);
    CHECK_X(px[10][10], WHITE);               /* a handle's inside is the paper's */
    CHECK_X(px[13][13], SEL);
    CHECK_X(px[7][27], SEL);                  /* N: the top's middle, 10 + 39 / 2 = 29, less 3 */
    CHECK_X(px[197][97], SEL);                /* the connector's ends */
    CHECK_X(px[197][197], SEL);
    CHECK_X(px[9][16], SEL);                  /* the dotted box round it, a pixel out */
    CHECK_X(px[9][19], WHITE);
    CHECK_N(odg_view_handle(&v, &doc, 10, 10, id, sizeof id), ODG_HANDLE_NW);
    CHECK(!strcmp(id, a));
    CHECK_N(odg_view_handle(&v, &doc, 29, 10, id, sizeof id), ODG_HANDLE_N);
    CHECK_N(odg_view_handle(&v, &doc, 49, 29, id, sizeof id), ODG_HANDLE_SE);
    CHECK_N(odg_view_handle(&v, &doc, 10, 19, id, sizeof id), ODG_HANDLE_W);
    CHECK_N(odg_view_handle(&v, &doc, 30, 20, id, sizeof id), ODG_HANDLE_NONE);
    CHECK_N(odg_view_handle(&v, &doc, 300, 300, id, sizeof id), ODG_HANDLE_NONE);
    v.nselected = 0;
    CHECK_N(odg_view_handle(&v, &doc, 10, 10, id, sizeof id), ODG_HANDLE_NONE);

    /* dragging a handle, snapped to the grid of 10 */
    odg_handle_drag(&v, &doc, box, ODG_HANDLE_SE, 13, 7, out);
    CHECK_N(out[0], PX(10)); CHECK_N(out[1], PX(10)); CHECK_N(out[2], PX(50)); CHECK_N(out[3], PX(30));
    odg_handle_drag(&v, &doc, box, ODG_HANDLE_W, 100, 0, out);     /* past the right side: one step left */
    CHECK_N(out[0], PX(40)); CHECK_N(out[2], PX(10)); CHECK_N(out[3], PX(20));
    odg_handle_drag(&v, &doc, box, ODG_HANDLE_N, 0, -3, out);
    CHECK_N(out[1], PX(10)); CHECK_N(out[3], PX(20));
    odg_handle_drag(&v, &doc, box, ODG_HANDLE_NW, -12, -12, out);
    CHECK_N(out[0], 0); CHECK_N(out[1], 0); CHECK_N(out[2], PX(50)); CHECK_N(out[3], PX(30));
    v.zoom = 200;                                                   /* 20 pixels at 200% are 10 on the page */
    odg_handle_drag(&v, &doc, box, ODG_HANDLE_E, 20, 0, out);
    CHECK_N(out[2], PX(50));
    done();
}

int main(void)
{
    d = cur = oldraw_new(&ops, NULL, W, H, NULL);
    test_places();
    test_colours();
    test_shapes();
    test_rounded_and_swimlane();
    test_labels();
    test_grid_and_area();
    test_selection_and_handles();
    oldraw_free(d);
    printf("diagram view: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
