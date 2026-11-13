/*
 * GFOpenDiag's model and .drawio file on the PC (owf_diagram.c, owf_drawio.c): numbers, styles, cells,
 * pages, and files read field by field, written, and read back to the same model, plain and compressed.
 * The sample files below were written by hand in the form diagrams.net writes them (an mxfile of
 * diagram pages, mxCell, mxGeometry, mxPoint, Array, UserObject); the compressed page was made on the
 * host the way its documentation describes (encodeURIComponent, raw DEFLATE, base64) with Python's
 * urllib.parse.quote, zlib and base64. Run under ASan and UBSan by make check.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <stdio.h>
#include <stdlib.h>
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

/* A two-page file as diagrams.net 24 writes one: shapes, a connector with bends and a label on it, a
 * group with its collapsed bounds, a UserObject with data and a link, a line break in a label. */
static const char sample[] =
    "<mxfile host=\"app.diagrams.net\" agent=\"test\" version=\"24.7.5\" type=\"device\">\n"
    "  <diagram id=\"p1\" name=\"Workbench\">\n"
    "    <mxGraphModel dx=\"1434\" dy=\"780\" grid=\"1\" gridSize=\"10\" guides=\"1\" tooltips=\"1\" connect=\"1\" "
    "arrows=\"1\" fold=\"1\" page=\"1\" pageScale=\"1\" pageWidth=\"827\" pageHeight=\"1169\" math=\"0\" shadow=\"0\">\n"
    "      <root>\n"
    "        <mxCell id=\"0\" />\n"
    "        <mxCell id=\"1\" parent=\"0\" />\n"
    "        <mxCell id=\"2\" value=\"A1200\" style=\"rounded=1;whiteSpace=wrap;html=1;fillColor=#dae8fc;\" vertex=\"1\" parent=\"1\">\n"
    "          <mxGeometry x=\"80\" y=\"120.5\" width=\"120\" height=\"60\" as=\"geometry\" />\n"
    "        </mxCell>\n"
    "        <mxCell id=\"3\" value=\"Monitor&#xa;1084S\" style=\"ellipse;html=1;\" vertex=\"1\" parent=\"1\">\n"
    "          <mxGeometry x=\"320\" y=\"120\" width=\"80.25\" height=\"80\" as=\"geometry\" />\n"
    "        </mxCell>\n"
    "        <mxCell id=\"4\" value=\"\" style=\"edgeStyle=orthogonalEdgeStyle;html=1;\" edge=\"1\" parent=\"1\" source=\"2\" target=\"3\">\n"
    "          <mxGeometry relative=\"1\" as=\"geometry\">\n"
    "            <mxPoint x=\"200\" y=\"150\" as=\"sourcePoint\" />\n"
    "            <Array as=\"points\">\n"
    "              <mxPoint x=\"260\" y=\"150\" />\n"
    "              <mxPoint x=\"260\" y=\"160\" />\n"
    "            </Array>\n"
    "          </mxGeometry>\n"
    "        </mxCell>\n"
    "        <mxCell id=\"5\" value=\"RGB\" style=\"edgeLabel;html=1;\" vertex=\"1\" connectable=\"0\" parent=\"4\">\n"
    "          <mxGeometry x=\"-0.2\" relative=\"1\" as=\"geometry\">\n"
    "            <mxPoint x=\"-5\" y=\"-10\" as=\"offset\" />\n"
    "          </mxGeometry>\n"
    "        </mxCell>\n"
    "        <mxCell id=\"g\" value=\"Desk\" style=\"swimlane;\" vertex=\"1\" collapsed=\"1\" parent=\"1\">\n"
    "          <mxGeometry x=\"40\" y=\"300\" width=\"160\" height=\"30\" as=\"geometry\">\n"
    "            <mxRectangle x=\"40\" y=\"300\" width=\"400\" height=\"200\" as=\"alternateBounds\" />\n"
    "          </mxGeometry>\n"
    "        </mxCell>\n"
    "        <mxCell id=\"g1\" value=\"Mouse\" style=\"\" vertex=\"1\" parent=\"g\">\n"
    "          <mxGeometry x=\"10\" y=\"40\" width=\"40\" height=\"20\" as=\"geometry\" />\n"
    "        </mxCell>\n"
    "      </root>\n"
    "    </mxGraphModel>\n"
    "  </diagram>\n"
    "  <diagram id=\"p2\" name=\"Links &amp; data\">\n"
    "    <mxGraphModel grid=\"0\">\n"
    "      <root>\n"
    "        <mxCell id=\"0\" />\n"
    "        <mxCell id=\"1\" parent=\"0\" />\n"
    "        <UserObject label=\"&lt;b&gt;A500&lt;/b&gt;\" link=\"https://example.org/a500\" owner=\"Dale\" id=\"u1\">\n"
    "          <mxCell style=\"shape=mxgraph.amiga.a500;html=1;\" vertex=\"1\" parent=\"1\">\n"
    "            <mxGeometry width=\"200\" height=\"90\" as=\"geometry\" />\n"
    "          </mxCell>\n"
    "        </UserObject>\n"
    "        <object label=\"note\" placeholders=\"1\" id=\"o1\">\n"
    "          <mxCell vertex=\"1\" parent=\"1\">\n"
    "            <mxGeometry x=\"-30\" y=\"10\" width=\"50\" height=\"20\" as=\"geometry\" />\n"
    "          </mxCell>\n"
    "        </object>\n"
    "      </root>\n"
    "    </mxGraphModel>\n"
    "  </diagram>\n"
    "</mxfile>\n";

/* One page compressed as diagrams.net does when asked (made on the host, see the top). Its XML: one
 * shape "a" labelled "Amiga 1200", a line break and an e acute, at 40,40 120x60. */
static const char compressed[] =
    "<mxfile host=\"app.diagrams.net\" compressed=\"true\"><diagram id=\"c1\" name=\"Packed\">"
    "jVFBbgMhDHyN7yxUkXpMt2lPOeUFKFgLEoQVcbpsX18IJtseIlVCwh57BjMGNYb8mfRsj9GgBylMBvUOUg5CiHJVZG3IrgNTcoabNuDkvrEzK6oOoMYU"
    "I7Uo5BF91e/UpvXxpMrCs054of8QdCN8aX/jMfbBTQUVg6xzl7MDqbIG9Qajgv1rI1xp9UxI8XYxWPWG0rRYR3ia9blWl+JQwSwFz2V+DhNhfjry8DCi"
    "mIwxIKW1tDDhhe1c/6aLM2SZz58WFt1kqa+hYfra8ukhvNlTAnaop9sm7rVfK1eHHw==</diagram></mxfile>";

static int read_text(const char *text, owf_diagram *d)
{
    owf_diagram_init(d);
    return owf_drawio_read((const unsigned char *)text, strlen(text), d);
}

static void test_numbers(void)
{
    char buf[24];
    CHECK_N(owf_dnum_parse("12"), 1200);
    CHECK_N(owf_dnum_parse("12.5"), 1250);
    CHECK_N(owf_dnum_parse("-0.2"), -20);
    CHECK_N(owf_dnum_parse("80.25"), 8025);
    CHECK_N(owf_dnum_parse("1.005"), 101);        /* rounded half away from zero */
    CHECK_N(owf_dnum_parse("-1.005"), -101);
    CHECK_N(owf_dnum_parse("1.004"), 100);
    CHECK_N(owf_dnum_parse("+3"), 300);
    CHECK_N(owf_dnum_parse(".5"), 50);
    CHECK_N(owf_dnum_parse("1e2"), 10000);
    CHECK_N(owf_dnum_parse("2.5E-1"), 25);
    CHECK_N(owf_dnum_parse("1.4210854715202004e-14"), 0);
    CHECK_N(owf_dnum_parse("abc"), 0);
    CHECK_N(owf_dnum_parse(""), 0);
    CHECK_N(owf_dnum_parse(NULL), 0);
    CHECK_N(owf_dnum_parse("99999999999999"), 200000000L);   /* cut to 2 million pixels */
    CHECK_N(owf_dnum_parse("-1e30"), -200000000L);
    owf_dnum_format(1250, buf, sizeof buf);
    CHECK_S(buf, "12.5");
    owf_dnum_format(-20, buf, sizeof buf);
    CHECK_S(buf, "-0.2");
    owf_dnum_format(8025, buf, sizeof buf);
    CHECK_S(buf, "80.25");
    owf_dnum_format(5, buf, sizeof buf);
    CHECK_S(buf, "0.05");
    owf_dnum_format(-300, buf, sizeof buf);
    CHECK_S(buf, "-3");
    owf_dnum_format(0, buf, sizeof buf);
    CHECK_S(buf, "0");
    owf_dnum_format(-2000000000L, buf, sizeof buf);
    CHECK_S(buf, "-20000000");
}

static void test_styles(void)
{
    char buf[32];
    char *style = NULL;
    const char *s = "rounded=1;whiteSpace=wrap;ellipse;fillColor=#dae8fc;";
    CHECK(owf_dstyle_get(s, "fillColor", buf, sizeof buf));
    CHECK_S(buf, "#dae8fc");
    CHECK(owf_dstyle_get(s, "ellipse", buf, sizeof buf));
    CHECK_S(buf, "");
    CHECK(!owf_dstyle_get(s, "fill", buf, sizeof buf));
    CHECK(!owf_dstyle_get(s, "rounded=1", buf, sizeof buf));
    CHECK(!owf_dstyle_get(NULL, "x", buf, sizeof buf));
    CHECK(owf_dstyle_get("a=123456789", "a", buf, 4));
    CHECK_S(buf, "123");
    CHECK(owf_dstyle_get("text;;html=1", "html", buf, sizeof buf));
    CHECK_S(buf, "1");

    CHECK_N(owf_dstyle_set(&style, "html", "1"), OWF_OK);
    CHECK_S(style, "html=1;");
    CHECK_N(owf_dstyle_set(&style, "fillColor", "#ffffff"), OWF_OK);
    CHECK_S(style, "html=1;fillColor=#ffffff;");
    CHECK_N(owf_dstyle_set(&style, "html", "0"), OWF_OK);
    CHECK_S(style, "html=0;fillColor=#ffffff;");
    CHECK_N(owf_dstyle_set(&style, "html", NULL), OWF_OK);
    CHECK_S(style, "fillColor=#ffffff;");
    CHECK_N(owf_dstyle_set(&style, "missing", NULL), OWF_OK);
    CHECK_S(style, "fillColor=#ffffff;");
    owf_free(style);
    style = NULL;
    CHECK_N(owf_dcell_set(&style, "ellipse;rounded=1"), OWF_OK);
    CHECK_N(owf_dstyle_set(&style, "rounded", "a-much-longer-value"), OWF_OK);
    CHECK_S(style, "ellipse;rounded=a-much-longer-value;");
    owf_free(style);
}

static void test_attrs(void)
{
    owf_dattrs a;
    memset(&a, 0, sizeof a);
    CHECK_N(owf_dattrs_set(&a, "grid", "1"), OWF_OK);
    CHECK_N(owf_dattrs_set(&a, "page", "0"), OWF_OK);
    CHECK_N(owf_dattrs_set(&a, "grid", "0"), OWF_OK);
    CHECK_N(a.n, 2);
    CHECK_S(a.v[0].name, "grid");
    CHECK_S(owf_dattrs_get(&a, "grid"), "0");
    CHECK(!owf_dattrs_get(&a, "none"));
    CHECK_N(owf_dattrs_set(&a, "grid", NULL), OWF_OK);
    CHECK_N(a.n, 1);
    CHECK_S(a.v[0].name, "page");
    owf_dattrs_free(&a);
    CHECK_N(a.n, 0);
}

static void test_model(void)
{
    owf_diagram d;
    owf_dpage *p;
    owf_dcell *c;
    char id[24];
    owf_diagram_init(&d);
    p = owf_diagram_add_page(&d, "page-1", "Page-1");
    CHECK(p != NULL);
    if (!p)
        return;
    CHECK_N(p->ncells, 2);
    CHECK_S(p->cells[0].id, "0");
    CHECK(p->cells[0].parent == NULL);
    CHECK_S(p->cells[1].parent, "0");

    c = owf_dpage_add(p, "box", "1", OWF_DCELL_VERTEX);
    CHECK(c != NULL);
    CHECK(owf_dpage_add(p, "box", "1", OWF_DCELL_VERTEX) == NULL);   /* the id is taken */
    c = owf_dpage_add(p, NULL, "1", OWF_DCELL_VERTEX);
    CHECK(c != NULL);
    CHECK_S(c->id, "3");
    owf_dpage_new_id(p, id, sizeof id);
    CHECK(owf_dpage_index(p, id) < 0);
    c = owf_dpage_add(p, "3x", "1", OWF_DCELL_EDGE);
    owf_dcell_set(&c->source, "box");
    owf_dcell_set(&c->target, "3");
    CHECK_N(owf_dgeo_add_point(&c->geo, 1000, 2000), OWF_OK);
    CHECK_N(c->geo.npoints, 1);
    c = owf_dpage_add(p, "inner", "box", OWF_DCELL_VERTEX);
    c = owf_dpage_add(p, "inner2", "inner", OWF_DCELL_VERTEX);
    c = owf_dpage_add(p, "label", "3x", OWF_DCELL_VERTEX);
    c = owf_dpage_add(p, "loose", "1", OWF_DCELL_EDGE);
    owf_dcell_set(&c->source, "3");
    CHECK_N(p->ncells, 9);
    /* the box, inside it inner and inner2, the edge joined to it and the label on that edge */
    CHECK_N(owf_dpage_remove(p, "box"), 5);
    CHECK_N(p->ncells, 4);
    CHECK(owf_dpage_find(p, "box") == NULL);
    CHECK(owf_dpage_find(p, "label") == NULL);
    CHECK(owf_dpage_find(p, "loose") != NULL);
    CHECK(owf_dpage_find(p, "3") != NULL);
    CHECK_N(owf_dpage_remove(p, "nothing"), 0);
    CHECK_N(owf_dpage_remove(p, "3"), 2);           /* and the loose edge from it */
    CHECK_N(p->ncells, 2);
    owf_diagram_free(&d);
    CHECK_N(d.npages, 0);
}

static void check_sample(const owf_diagram *d)
{
    const owf_dpage *p, *q;
    const owf_dcell *c;
    CHECK_N(d->npages, 2);
    if (d->npages != 2)
        return;
    CHECK_S(owf_dattrs_get(&d->file, "version"), "24.7.5");
    p = &d->pages[0];
    CHECK_S(p->id, "p1");
    CHECK_S(p->name, "Workbench");
    CHECK_N(p->model.n, 15);
    CHECK_S(owf_dattrs_get(&p->model, "pageWidth"), "827");
    CHECK_N(p->ncells, 8);
    CHECK(!p->compressed);

    c = owf_dpage_find(p, "2");
    CHECK(c && c->kind == OWF_DCELL_VERTEX);
    if (c) {
        CHECK_S(c->value, "A1200");
        CHECK_S(c->parent, "1");
        CHECK_N(c->geo.flags, OWF_DGEO_HAS);
        CHECK_N(c->geo.x, 8000);
        CHECK_N(c->geo.y, 12050);
        CHECK_N(c->geo.width, 12000);
        CHECK_N(c->geo.height, 6000);
    }
    c = owf_dpage_find(p, "3");
    CHECK(c != NULL);
    if (c) {
        CHECK_S(c->value, "Monitor\n1084S");
        CHECK_N(c->geo.width, 8025);
    }
    c = owf_dpage_find(p, "4");
    CHECK(c && c->kind == OWF_DCELL_EDGE);
    if (c) {
        CHECK_S(c->value, "");
        CHECK_S(c->source, "2");
        CHECK_S(c->target, "3");
        CHECK(c->geo.flags & OWF_DGEO_RELATIVE);
        CHECK(c->geo.flags & OWF_DGEO_SOURCE_POINT);
        CHECK(!(c->geo.flags & OWF_DGEO_TARGET_POINT));
        CHECK_N(c->geo.source_point.x, 20000);
        CHECK_N(c->geo.npoints, 2);
        if (c->geo.npoints == 2) {
            CHECK_N(c->geo.points[1].x, 26000);
            CHECK_N(c->geo.points[1].y, 16000);
        }
    }
    c = owf_dpage_find(p, "5");
    CHECK(c != NULL);
    if (c) {
        CHECK_S(c->parent, "4");
        CHECK_S(owf_dattrs_get(&c->extra, "connectable"), "0");
        CHECK_N(c->geo.x, -20);
        CHECK(c->geo.flags & OWF_DGEO_OFFSET);
        CHECK_N(c->geo.offset.x, -500);
        CHECK_N(c->geo.offset.y, -1000);
    }
    c = owf_dpage_find(p, "g");
    CHECK(c != NULL);
    if (c) {
        CHECK_S(owf_dattrs_get(&c->extra, "collapsed"), "1");
        CHECK(c->geo.flags & OWF_DGEO_ALTERNATE);
        CHECK_N(c->geo.alternate[2], 40000);
    }
    c = owf_dpage_find(p, "g1");
    CHECK(c && c->style && !*c->style);

    q = &d->pages[1];
    CHECK_S(q->name, "Links & data");
    CHECK_N(q->ncells, 4);
    c = owf_dpage_find(q, "u1");
    CHECK(c && c->wrapper == OWF_DWRAP_USEROBJECT);
    if (c) {
        CHECK_S(c->value, "<b>A500</b>");
        CHECK_S(owf_dattrs_get(&c->data, "link"), "https://example.org/a500");
        CHECK_S(owf_dattrs_get(&c->data, "owner"), "Dale");
        CHECK_N(c->data.n, 2);
        CHECK_S(c->style, "shape=mxgraph.amiga.a500;html=1;");
        CHECK_N(c->geo.x, 0);
        CHECK_N(c->geo.width, 20000);
    }
    c = owf_dpage_find(q, "o1");
    CHECK(c && c->wrapper == OWF_DWRAP_OBJECT);
    if (c) {
        CHECK_S(c->value, "note");
        CHECK(c->style == NULL);
        CHECK_N(c->geo.x, -3000);
    }
}

/* Every field of every cell the same in both. */
static int same_model(const owf_diagram *a, const owf_diagram *b)
{
    int i, j, k;
#define SAME_S(x, y) ((!(x) && !(y)) || ((x) && (y) && !strcmp((x), (y))))
    if (a->npages != b->npages)
        return 0;
    for (i = 0; i < a->npages; i++) {
        const owf_dpage *p = &a->pages[i], *q = &b->pages[i];
        if (!SAME_S(p->id, q->id) || !SAME_S(p->name, q->name) || p->ncells != q->ncells || p->model.n != q->model.n)
            return 0;
        for (k = 0; k < p->model.n; k++)
            if (!SAME_S(owf_dattrs_get(&q->model, p->model.v[k].name), p->model.v[k].value))
                return 0;
        for (j = 0; j < p->ncells; j++) {
            const owf_dcell *c = &p->cells[j], *e = &q->cells[j];
            if (!SAME_S(c->id, e->id) || !SAME_S(c->parent, e->parent) || !SAME_S(c->value, e->value) ||
                !SAME_S(c->style, e->style) || !SAME_S(c->source, e->source) || !SAME_S(c->target, e->target) ||
                c->kind != e->kind || c->wrapper != e->wrapper || c->data.n != e->data.n ||
                c->extra.n != e->extra.n || c->geo.flags != e->geo.flags || c->geo.x != e->geo.x ||
                c->geo.y != e->geo.y || c->geo.width != e->geo.width || c->geo.height != e->geo.height ||
                c->geo.npoints != e->geo.npoints || c->geo.source_point.x != e->geo.source_point.x ||
                c->geo.source_point.y != e->geo.source_point.y || c->geo.target_point.x != e->geo.target_point.x ||
                c->geo.offset.x != e->geo.offset.x || c->geo.offset.y != e->geo.offset.y ||
                memcmp(c->geo.alternate, e->geo.alternate, sizeof c->geo.alternate))
                return 0;
            for (k = 0; k < c->geo.npoints; k++)
                if (c->geo.points[k].x != e->geo.points[k].x || c->geo.points[k].y != e->geo.points[k].y)
                    return 0;
            for (k = 0; k < c->data.n; k++)
                if (!SAME_S(owf_dattrs_get(&e->data, c->data.v[k].name), c->data.v[k].value))
                    return 0;
            for (k = 0; k < c->extra.n; k++)
                if (!SAME_S(owf_dattrs_get(&e->extra, c->extra.v[k].name), c->extra.v[k].value))
                    return 0;
        }
    }
    return 1;
#undef SAME_S
}

static void test_sample(void)
{
    owf_diagram d, back, again;
    unsigned char *out = NULL, *out2 = NULL;
    size_t n = 0, n2 = 0;
    CHECK_N(read_text(sample, &d), OWF_OK);
    check_sample(&d);

    /* plain: read back the same, and written again the same bytes */
    CHECK_N(owf_drawio_write(&d, 0, &out, &n), OWF_OK);
    CHECK(out && strlen((char *)out) == n);
    if (out) {
        CHECK_HAS(out, "<mxfile host=\"app.diagrams.net\" agent=\"test\" version=\"24.7.5\" type=\"device\">");
        CHECK_HAS(out, "<diagram id=\"p2\" name=\"Links &amp; data\">");
        CHECK_HAS(out, "value=\"Monitor&#xa;1084S\"");
        CHECK_HAS(out, "<mxGeometry x=\"80\" y=\"120.5\" width=\"120\" height=\"60\" as=\"geometry\" />");
        CHECK_HAS(out, "<mxGeometry x=\"-0.2\" relative=\"1\" as=\"geometry\">");
        CHECK_HAS(out, "<mxCell id=\"4\" value=\"\" style=\"edgeStyle=orthogonalEdgeStyle;html=1;\" edge=\"1\" "
                       "parent=\"1\" source=\"2\" target=\"3\">");
        CHECK_HAS(out, "<UserObject label=\"&lt;b&gt;A500&lt;/b&gt;\" link=\"https://example.org/a500\" "
                       "owner=\"Dale\" id=\"u1\">");
        CHECK_HAS(out, "<mxRectangle x=\"40\" y=\"300\" width=\"400\" height=\"200\" as=\"alternateBounds\" />");
        CHECK_HAS(out, "<mxCell id=\"0\" />");
        CHECK_NOT(out, "x=\"0\"");
        CHECK_N(read_text((const char *)out, &back), OWF_OK);
        check_sample(&back);
        CHECK(same_model(&d, &back));
        CHECK_N(owf_drawio_write(&back, 0, &out2, &n2), OWF_OK);
        CHECK(n2 == n && out2 && !memcmp(out, out2, n));
        owf_diagram_free(&back);
        owf_free(out2);
        owf_free(out);
    }

    /* compressed: the pages are base64 text, and read back the same */
    out = NULL;
    CHECK_N(owf_drawio_write(&d, 1, &out, &n), OWF_OK);
    if (out) {
        CHECK_NOT(out, "<mxGraphModel");
        CHECK_HAS(out, "<diagram id=\"p1\" name=\"Workbench\">");
        CHECK_N(read_text((const char *)out, &again), OWF_OK);
        CHECK(same_model(&d, &again));
        CHECK(again.npages == 2 && again.pages[0].compressed && again.pages[1].compressed);
        owf_diagram_free(&again);
        owf_free(out);
    }
    owf_diagram_free(&d);
}

static void test_compressed(void)
{
    owf_diagram d;
    const owf_dcell *c;
    CHECK_N(read_text(compressed, &d), OWF_OK);
    CHECK_N(d.npages, 1);
    if (d.npages == 1) {
        CHECK(d.pages[0].compressed);
        CHECK_S(d.pages[0].name, "Packed");
        CHECK_S(owf_dattrs_get(&d.pages[0].model, "gridSize"), "10");
        CHECK_N(d.pages[0].ncells, 3);
        c = owf_dpage_find(&d.pages[0], "a");
        CHECK(c != NULL);
        if (c) {
            CHECK_S(c->value, "Amiga 1200 \n\xc3\xa9");
            CHECK_S(c->style, "rounded=1;whiteSpace=wrap;html=1;");
            CHECK_N(c->geo.width, 12000);
        }
    }
    owf_diagram_free(&d);
}

static void test_bare_and_bad(void)
{
    owf_diagram d;
    /* a bare mxGraphModel, as older files and the clipboard hold */
    CHECK_N(read_text("<mxGraphModel><root><mxCell id=\"0\"/><mxCell id=\"1\" parent=\"0\"/>"
                      "<mxCell id=\"x\" vertex=\"1\" parent=\"1\"><mxGeometry width=\"10\" height=\"10\" as=\"geometry\"/>"
                      "</mxCell></root></mxGraphModel>", &d), OWF_OK);
    CHECK_N(d.npages, 1);
    CHECK_N(d.pages[0].ncells, 3);
    owf_diagram_free(&d);

    /* cells with no id, or one already taken, get new ones */
    CHECK_N(read_text("<mxfile><diagram><mxGraphModel><root><mxCell id=\"0\"/><mxCell id=\"1\" parent=\"0\"/>"
                      "<mxCell vertex=\"1\" parent=\"1\"/><mxCell id=\"1\" vertex=\"1\" parent=\"1\"/>"
                      "</root></mxGraphModel></diagram></mxfile>", &d), OWF_OK);
    CHECK_N(d.pages[0].ncells, 4);
    CHECK(strcmp(d.pages[0].cells[2].id, d.pages[0].cells[3].id) != 0);
    CHECK(strcmp(d.pages[0].cells[3].id, "1") != 0);
    owf_diagram_free(&d);

    /* an empty page, and an empty mxfile */
    CHECK_N(read_text("<mxfile><diagram id=\"e\" name=\"Empty\"></diagram></mxfile>", &d), OWF_OK);
    CHECK_N(d.npages, 1);
    CHECK_N(d.pages[0].ncells, 0);
    owf_diagram_free(&d);
    CHECK_N(read_text("<mxfile/>", &d), OWF_OK);
    CHECK_N(d.npages, 0);
    owf_diagram_free(&d);

    CHECK_N(read_text("<svg xmlns=\"http://www.w3.org/2000/svg\"/>", &d), OWF_ERR_FORMAT);
    owf_diagram_free(&d);
    CHECK_N(read_text("not xml at all <<<", &d), OWF_ERR_FORMAT);
    owf_diagram_free(&d);
    CHECK_N(read_text("<mxfile><diagram>!!!not base64!!!</diagram></mxfile>", &d), OWF_ERR_CORRUPT);
    owf_diagram_free(&d);
    /* base64 of bytes that are not DEFLATE */
    CHECK_N(read_text("<mxfile><diagram>/////////////w==</diagram></mxfile>", &d), OWF_ERR_CORRUPT);
    owf_diagram_free(&d);
    /* DEFLATE (one stored block, "abc") that is not an mxGraphModel */
    CHECK_N(read_text("<mxfile><diagram>AQMA/P9hYmM=</diagram></mxfile>", &d), OWF_ERR_CORRUPT);
    owf_diagram_free(&d);
}

static void test_new_diagram(void)
{
    owf_diagram d, back;
    owf_dpage *p;
    owf_dcell *c;
    unsigned char *out = NULL;
    size_t n = 0;
    int i;
    char id[64];
    owf_diagram_init(&d);
    p = owf_diagram_add_page(&d, "n1", "Mind map");
    if (!p)
        return;
    owf_dattrs_set(&p->model, "grid", "1");
    for (i = 0; i < 300; i++) {
        c = owf_dpage_add(p, NULL, "1", OWF_DCELL_VERTEX);
        if (!c)
            break;
        snprintf(id, sizeof id, "Idea %d \"quoted\" & <tag>\t\r", i);
        owf_dcell_set(&c->value, id);
        owf_dstyle_set(&c->style, "ellipse", "");
        c->geo.flags = OWF_DGEO_HAS;
        c->geo.x = i * 1234;
        c->geo.y = -i * 50;
        c->geo.width = 8000;
        c->geo.height = 4000;
    }
    CHECK_N(p->ncells, 302);
    CHECK_N(owf_drawio_write(&d, 0, &out, &n), OWF_OK);
    if (out) {
        CHECK_HAS(out, "<mxfile host=\"GFOpenDiag\">");
        CHECK_HAS(out, "value=\"Idea 7 &quot;quoted&quot; &amp; &lt;tag&gt;&#x9;&#xd;\"");
        CHECK_N(read_text((const char *)out, &back), OWF_OK);
        CHECK(same_model(&d, &back));
        owf_diagram_free(&back);
        owf_free(out);
    }
    /* compressed, bigger than one stored block when zlib is not there */
    out = NULL;
    CHECK_N(owf_drawio_write(&d, 1, &out, &n), OWF_OK);
    if (out) {
        CHECK_N(read_text((const char *)out, &back), OWF_OK);
        CHECK(same_model(&d, &back));
        owf_diagram_free(&back);
        owf_free(out);
    }
    owf_diagram_free(&d);
}

int main(void)
{
    test_numbers();
    test_styles();
    test_attrs();
    test_model();
    test_sample();
    test_compressed();
    test_bare_and_bad();
    test_new_diagram();
    printf("diagram: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
