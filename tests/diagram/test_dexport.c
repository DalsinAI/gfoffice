/*
 * GFOpenDiag's SVG and PNG with the diagram inside (owf_dsvg.c, owf_dpng.c) on the PC: where cells are,
 * what the SVG draws, and both files read back to the same model. The PNGs below were made on the host
 * with Python's zlib and struct from the PNG specification: a 1x1 picture, and the same with a zTXt
 * chunk keyed "mxfile" as older diagrams.net exports have. Run under ASan and UBSan by make check.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <stdio.h>
#include <string.h>
#include "owf_diagram.h"
#include "owf_internal.h"

static int fails, checks;

#define CHECK(cond) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_N(got, want) do { long g_ = (long)(got), w_ = (long)(want); checks++; if (g_ != w_) { fails++; \
    printf("FAIL %s:%d: %s is %ld, want %ld\n", __FILE__, __LINE__, #got, g_, w_); } } while (0)
#define CHECK_S(got, want) do { const char *g_ = (got), *w_ = (want); checks++; if (!g_ || strcmp(g_, w_) != 0) { \
    fails++; printf("FAIL %s:%d: %s is \"%s\", want \"%s\"\n", __FILE__, __LINE__, #got, g_ ? g_ : "(null)", w_); } } while (0)
#define CHECK_HAS(text, part) do { checks++; if (!strstr((const char *)(text), (part))) { fails++; \
    printf("FAIL %s:%d: missing %s\n", __FILE__, __LINE__, (part)); } } while (0)
#define CHECK_NOT(text, part) do { checks++; if (strstr((const char *)(text), (part))) { fails++; \
    printf("FAIL %s:%d: has %s\n", __FILE__, __LINE__, (part)); } } while (0)

static const unsigned char tiny_png[69] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x02, 0x00, 0x00, 0x00, 0x90, 0x77, 0x53,
    0xde, 0x00, 0x00, 0x00, 0x0c, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0xf8, 0xcf, 0xc0, 0x00,
    0x00, 0x03, 0x01, 0x01, 0x00, 0xc9, 0xfe, 0x92, 0xef, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e,
    0x44, 0xae, 0x42, 0x60, 0x82,
};

static const unsigned char ztxt_png[245] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x02, 0x00, 0x00, 0x00, 0x90, 0x77, 0x53,
    0xde, 0x00, 0x00, 0x00, 0xa4, 0x7a, 0x54, 0x58, 0x74, 0x6d, 0x78, 0x66, 0x69, 0x6c, 0x65, 0x00,
    0x00, 0x78, 0x9c, 0x8d, 0x50, 0xcb, 0x0e, 0x02, 0x21, 0x0c, 0xfc, 0x9a, 0xde, 0x11, 0xe2, 0x17,
    0xa0, 0x72, 0xf2, 0xf1, 0x0b, 0x24, 0x34, 0x0b, 0x09, 0xc8, 0x06, 0xd1, 0x65, 0xfd, 0x7a, 0x09,
    0x54, 0x37, 0x7b, 0xd8, 0xc4, 0x43, 0x93, 0xce, 0x4c, 0x67, 0xd2, 0x16, 0x84, 0x0c, 0x45, 0x25,
    0x3d, 0xda, 0x73, 0x34, 0xe8, 0x41, 0x1c, 0x41, 0xc8, 0x14, 0x63, 0xee, 0x5d, 0x28, 0x12, 0xbd,
    0x07, 0xce, 0x9c, 0x01, 0x71, 0x00, 0xce, 0x59, 0x2d, 0xe0, 0xa7, 0x0d, 0x75, 0xd7, 0x54, 0x36,
    0xea, 0x84, 0xf7, 0xfc, 0x8f, 0xe1, 0xdd, 0x0d, 0x2f, 0xed, 0x9f, 0xd8, 0x99, 0xab, 0x37, 0x95,
    0xb8, 0x5d, 0x14, 0x29, 0x98, 0x32, 0x96, 0xcd, 0xf4, 0x46, 0x51, 0xb4, 0xc2, 0x18, 0x30, 0xa7,
    0xb9, 0x8e, 0x90, 0x61, 0xdf, 0x0d, 0xf3, 0x0a, 0x4d, 0xce, 0x64, 0x4b, 0x4c, 0x5f, 0x8e, 0x59,
    0x74, 0x83, 0xa5, 0x44, 0x5a, 0x98, 0xe9, 0x47, 0xc7, 0xc3, 0x2f, 0x75, 0x39, 0xa3, 0x36, 0x74,
    0xc9, 0x17, 0x2e, 0x1f, 0x6b, 0xda, 0xea, 0xa1, 0x1f, 0x38, 0xb2, 0x5f, 0x29, 0x1d, 0x7d, 0xd5,
    0x40, 0x00, 0x00, 0x00, 0x0c, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0xf8, 0xcf, 0xc0, 0x00,
    0x00, 0x03, 0x01, 0x01, 0x00, 0xc9, 0xfe, 0x92, 0xef, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e,
    0x44, 0xae, 0x42, 0x60, 0x82,
};

/* A page: a group holding a box, an ellipse, a rhombus, a connector between box and ellipse with a bend
 * and a label along it, a hidden layer, and an HTML label; a second page. */
static const char sample[] =
    "<mxfile host=\"test\"><diagram id=\"a\" name=\"One\"><mxGraphModel><root>"
    "<mxCell id=\"0\"/><mxCell id=\"1\" parent=\"0\"/>"
    "<mxCell id=\"g\" value=\"Desk\" style=\"swimlane;startSize=20;\" vertex=\"1\" parent=\"1\">"
    "<mxGeometry x=\"100\" y=\"50\" width=\"300\" height=\"200\" as=\"geometry\"/></mxCell>"
    "<mxCell id=\"box\" value=\"A1200\" style=\"rounded=1;fillColor=#dae8fc;strokeColor=#6c8ebf;\" vertex=\"1\" parent=\"g\">"
    "<mxGeometry x=\"20\" y=\"40\" width=\"80\" height=\"40\" as=\"geometry\"/></mxCell>"
    "<mxCell id=\"mon\" value=\"&lt;b&gt;1084S&lt;/b&gt;&lt;br&gt;monitor &amp;amp; speakers\" "
    "style=\"ellipse;html=1;fontSize=10;fontStyle=1;\" vertex=\"1\" parent=\"1\">"
    "<mxGeometry x=\"500\" y=\"90\" width=\"100\" height=\"60\" as=\"geometry\"/></mxCell>"
    "<mxCell id=\"q\" value=\"OK?\" style=\"rhombus;dashed=1;\" vertex=\"1\" parent=\"1\">"
    "<mxGeometry x=\"500\" y=\"300\" width=\"60\" height=\"60\" as=\"geometry\"/></mxCell>"
    "<mxCell id=\"e\" value=\"RGB\" style=\"endArrow=classic;strokeColor=#ff0000;\" edge=\"1\" parent=\"1\" source=\"box\" target=\"mon\">"
    "<mxGeometry relative=\"1\" as=\"geometry\"><Array as=\"points\"><mxPoint x=\"300\" y=\"110\"/></Array></mxGeometry></mxCell>"
    "<mxCell id=\"el\" value=\"23-pin\" style=\"edgeLabel;\" vertex=\"1\" connectable=\"0\" parent=\"e\">"
    "<mxGeometry x=\"-0.5\" relative=\"1\" as=\"geometry\"><mxPoint y=\"-10\" as=\"offset\"/></mxGeometry></mxCell>"
    "<mxCell id=\"loose\" style=\"endArrow=none;\" edge=\"1\" parent=\"1\" source=\"q\">"
    "<mxGeometry relative=\"1\" as=\"geometry\"><mxPoint x=\"530\" y=\"500\" as=\"targetPoint\"/></mxGeometry></mxCell>"
    "<mxCell id=\"hid\" parent=\"0\" visible=\"0\"/>"
    "<mxCell id=\"secret\" value=\"Hidden\" vertex=\"1\" parent=\"hid\">"
    "<mxGeometry x=\"2000\" y=\"2000\" width=\"10\" height=\"10\" as=\"geometry\"/></mxCell>"
    "</root></mxGraphModel></diagram>"
    "<diagram id=\"b\" name=\"Two\"><mxGraphModel><root><mxCell id=\"0\"/><mxCell id=\"1\" parent=\"0\"/>"
    "<mxCell id=\"t\" value=\"x &lt; y\" style=\"text;\" vertex=\"1\" parent=\"1\">"
    "<mxGeometry x=\"-40\" y=\"-20\" width=\"40\" height=\"20\" as=\"geometry\"/></mxCell>"
    "</root></mxGraphModel></diagram></mxfile>";

static int read_text(const char *text, owf_diagram *d)
{
    owf_diagram_init(d);
    return owf_drawio_read((const unsigned char *)text, strlen(text), d);
}

/* The same number of pages and cells, and every cell's id, value and box. */
static int same(const owf_diagram *a, const owf_diagram *b)
{
    int i, j;
    if (a->npages != b->npages)
        return 0;
    for (i = 0; i < a->npages; i++) {
        const owf_dpage *p = &a->pages[i], *q = &b->pages[i];
        if (p->ncells != q->ncells)
            return 0;
        for (j = 0; j < p->ncells; j++) {
            const owf_dcell *c = &p->cells[j], *e = &q->cells[j];
            if (strcmp(c->id, e->id) || (c->value == NULL) != (e->value == NULL) ||
                (c->value && strcmp(c->value, e->value)) || c->geo.x != e->geo.x || c->geo.y != e->geo.y ||
                c->geo.width != e->geo.width || c->geo.height != e->geo.height || c->kind != e->kind)
                return 0;
        }
    }
    return 1;
}

static void test_places(void)
{
    owf_diagram d;
    const owf_dpage *p;
    owf_dnum box[4];
    CHECK_N(read_text(sample, &d), OWF_OK);
    p = &d.pages[0];
    /* a box inside the group is moved by the group's place */
    CHECK(owf_dcell_box(p, owf_dpage_find(p, "box"), box));
    CHECK_N(box[0], 12000);
    CHECK_N(box[1], 9000);
    CHECK_N(box[2], 8000);
    CHECK(owf_dcell_box(p, owf_dpage_find(p, "g"), box));
    CHECK_N(box[0], 10000);
    /* a label along an edge, and the layers, have no box of their own */
    CHECK(!owf_dcell_box(p, owf_dpage_find(p, "el"), box));
    CHECK(!owf_dcell_box(p, owf_dpage_find(p, "1"), box));
    /* the page: from the group's corner to the loose end at 530,500; the hidden layer's cell is left out */
    CHECK(owf_dpage_bounds(p, box));
    CHECK_N(box[0], 10000);
    CHECK_N(box[1], 5000);
    CHECK_N(box[2], 50000);
    CHECK_N(box[3], 45000);
    CHECK(owf_dpage_bounds(&d.pages[1], box));
    CHECK_N(box[0], -4000);
    owf_diagram_free(&d);
    {
        owf_diagram e;
        owf_diagram_init(&e);
        CHECK(!owf_dpage_bounds(owf_diagram_add_page(&e, "x", "Empty"), box));
        owf_diagram_free(&e);
    }
}

static void test_svg(void)
{
    owf_diagram d, back;
    unsigned char *svg = NULL, *svg2 = NULL;
    size_t n = 0, n2 = 0;
    CHECK_N(read_text(sample, &d), OWF_OK);
    CHECK_N(owf_dsvg_write(&d, 5, &svg, &n), OWF_ERR_FORMAT);
    CHECK_N(owf_dsvg_write(&d, 0, &svg, &n), OWF_OK);
    if (!svg)
        return;
    CHECK(strlen((char *)svg) == n);
    CHECK_HAS(svg, "<svg xmlns=\"http://www.w3.org/2000/svg\" version=\"1.1\" width=\"502px\" height=\"452px\" "
                   "viewBox=\"99 49 502 452\" content=\"&lt;mxfile host=&quot;test&quot;&gt;");
    /* the group, its header line and its label in the header */
    CHECK_HAS(svg, "<rect x=\"100\" y=\"50\" width=\"300\" height=\"200\" fill=\"#ffffff\" stroke=\"#000000\" />");
    CHECK_HAS(svg, "<path d=\"M 100 70 L 400 70\" fill=\"none\" stroke=\"#000000\" />");
    CHECK_HAS(svg, ">Desk</tspan>");
    /* the rounded box where the group puts it, 15% of its height rounded off */
    CHECK_HAS(svg, "<rect x=\"120\" y=\"90\" width=\"80\" height=\"40\" rx=\"3\" ry=\"3\" fill=\"#dae8fc\" stroke=\"#6c8ebf\" />");
    CHECK_HAS(svg, "<ellipse cx=\"550\" cy=\"120\" rx=\"50\" ry=\"30\" fill=\"#ffffff\" stroke=\"#000000\" />");
    CHECK_HAS(svg, "<path d=\"M 530 300 L 560 330 L 530 360 L 500 330 Z\" fill=\"#ffffff\" stroke=\"#000000\" "
                   "stroke-dasharray=\"3 3\" />");
    /* the connector: from the box's right side to the bend, then to the ellipse's box, in red, with a head */
    CHECK_HAS(svg, "<path d=\"M 200 110 L 300 110 L 500 118\" fill=\"none\" stroke=\"#ff0000\" />");
    CHECK_HAS(svg, "fill=\"#ff0000\" stroke=\"#ff0000\" />");
    CHECK_HAS(svg, ">RGB</tspan>");
    /* edge labels sit on white so the line doesn't run through them; shapes' labels have none */
    CHECK_HAS(svg, "fill=\"#ffffff\" stroke=\"none\" />\n<text x=\"350.03\"");
    CHECK_NOT(svg, "stroke=\"none\" />\n<text x=\"160\"");
    /* the label along it, a quarter of the way, 10 up */
    CHECK_HAS(svg, ">23-pin</tspan>");
    /* the loose end, with no head */
    CHECK_HAS(svg, "<path d=\"M 530 360 L 530 500\" fill=\"none\" stroke=\"#000000\" />");
    /* the HTML label: tags out, the break a new line, its entity read and escaped again, bold */
    CHECK_HAS(svg, ">1084S</tspan>");
    CHECK_HAS(svg, ">monitor &amp; speakers</tspan>");
    CHECK_HAS(svg, "font-size=\"10\" fill=\"#000000\" font-weight=\"bold\">");
    CHECK_NOT(svg, "&lt;b&gt;1084S");
    CHECK_NOT(svg, ">Hidden<");
    CHECK_NOT(svg, "\n\">");
    /* read back: the whole diagram, both pages */
    CHECK_N(owf_dsvg_read(svg, n, &back), OWF_OK);
    CHECK(same(&d, &back));
    CHECK_N(back.npages, 2);
    owf_diagram_free(&back);

    /* the second page: text with no shape, its < escaped */
    CHECK_N(owf_dsvg_write(&d, 1, &svg2, &n2), OWF_OK);
    if (svg2) {
        CHECK_HAS(svg2, "viewBox=\"-41 -21 42 22\"");
        CHECK_NOT(svg2, "<rect");
        CHECK_HAS(svg2, ">x &lt; y</tspan>");
        owf_free(svg2);
    }
    owf_free(svg);
    owf_diagram_free(&d);

    /* not ours */
    CHECK_N(owf_dsvg_read((const unsigned char *)"<svg xmlns=\"http://www.w3.org/2000/svg\"/>", 41, &back), OWF_ERR_FORMAT);
    owf_diagram_free(&back);
    owf_diagram_init(&back);
    CHECK_N(owf_dsvg_read((const unsigned char *)"<mxfile/>", 9, &back), OWF_ERR_FORMAT);
    owf_diagram_free(&back);
}

static void test_png(void)
{
    owf_diagram d, back;
    unsigned char *png = NULL, *png2 = NULL;
    size_t n = 0, n2 = 0;
    CHECK_N(read_text(sample, &d), OWF_OK);
    CHECK_N(owf_dpng_embed(tiny_png, sizeof tiny_png, &d, &png, &n), OWF_OK);
    if (png) {
        /* signature, IHDR, then ours */
        CHECK(n > sizeof tiny_png);
        CHECK(!memcmp(png, tiny_png, 33));
        CHECK(!memcmp(png + 37, "tEXtmxfile\0%3Cmxfile", 20));
        CHECK(!memcmp(png + n - 36, tiny_png + 33, 36));
        CHECK_N(owf_dpng_read(png, n, &back), OWF_OK);
        CHECK(same(&d, &back));
        /* put in again: the old one is replaced, not kept beside it */
        CHECK_N(owf_dpng_embed(png, n, &back, &png2, &n2), OWF_OK);
        CHECK_N(n2, n);
        CHECK(png2 && !memcmp(png, png2, n));
        owf_diagram_free(&back);
        owf_free(png2);
        /* a damaged CRC */
        png[40] ^= 1;
        CHECK_N(owf_dpng_read(png, n, &back), OWF_ERR_CORRUPT);
        owf_diagram_free(&back);
        owf_free(png);
    }
    owf_diagram_init(&back);
    CHECK_N(owf_dpng_read(tiny_png, sizeof tiny_png, &back), OWF_ERR_FORMAT);
    CHECK_N(owf_dpng_read((const unsigned char *)"GIF89a", 6, &back), OWF_ERR_FORMAT);
    CHECK_N(owf_dpng_embed((const unsigned char *)"GIF89a", 6, &d, &png, &n), OWF_ERR_FORMAT);
    CHECK_N(owf_dpng_embed(tiny_png, 40, &d, &png, &n), OWF_ERR_FORMAT);   /* cut short */
    owf_diagram_free(&back);

    /* an older export's zTXt */
    CHECK_N(owf_dpng_read(ztxt_png, sizeof ztxt_png, &back), OWF_OK);
    CHECK_N(back.npages, 1);
    if (back.npages == 1) {
        const owf_dcell *c = owf_dpage_find(&back.pages[0], "z");
        CHECK(c != NULL);
        if (c)
            CHECK_S(c->value, "Old PNG");
    }
    owf_diagram_free(&back);
    owf_diagram_free(&d);
}

int main(void)
{
    test_places();
    test_svg();
    test_png();
    printf("diagram export: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
