/*
 * The PDF writer of libowf/diagram/owf_dpdf.c on the PC: pages the size of what is on them, each shape drawn
 * with its fill, line and dashes, flowchart shapes as paths, connectors with arrowheads, labels as text in the
 * standard fonts placed and coloured, every page or one, and an empty page. Run under ASan and UBSan by make
 * diagram-check.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <stdio.h>
#include <string.h>
#include "owf_diagram.h"

static int fails, checks;

#define CHECK(cond) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_N(got, want) do { long g_ = (long)(got), w_ = (long)(want); checks++; if (g_ != w_) { fails++; \
    printf("FAIL %s:%d: %s is %ld, want %ld\n", __FILE__, __LINE__, #got, g_, w_); } } while (0)

static owf_dcell *shape(owf_dpage *p, const char *id, const char *style, long x, long y, long w, long h,
                        const char *value)
{
    owf_dcell *c = owf_dpage_add(p, id, "1", OWF_DCELL_VERTEX);
    c->geo.flags = OWF_DGEO_HAS;
    c->geo.x = OWF_DNUM(x);
    c->geo.y = OWF_DNUM(y);
    c->geo.width = OWF_DNUM(w);
    c->geo.height = OWF_DNUM(h);
    owf_dcell_set(&c->style, style);
    if (value)
        owf_dcell_set(&c->value, value);
    return c;
}

static int count(const char *hay, size_t n, const char *needle)
{
    size_t k = strlen(needle), i;
    int got = 0;
    for (i = 0; i + k <= n; i++)
        if (!memcmp(hay + i, needle, k))
            got++;
    return got;
}

static const char *find(const char *hay, size_t n, const char *needle)
{
    size_t k = strlen(needle), i;
    for (i = 0; i + k <= n; i++)
        if (!memcmp(hay + i, needle, k))
            return hay + i;
    return NULL;
}

static void test_shapes(void)
{
    owf_diagram d;
    owf_dpage *p;
    owf_dcell *e;
    unsigned char *pdf = NULL;
    size_t n = 0;
    const char *s;
    owf_diagram_init(&d);
    p = owf_diagram_add_page(&d, "p1", "Flow");
    /* from 100,100 to 400,300: a page 300 + 20 by 200 + 20 pixels, 240 by 165 points */
    shape(p, "box", "rounded=0;whiteSpace=wrap;html=1;fillColor=#ff0000;", 100, 100, 120, 60, "Start &amp; go");
    shape(p, "round", "rounded=1;arcSize=50;html=1;dashed=1;strokeColor=#0000ff;", 280, 100, 120, 40, NULL);
    shape(p, "oval", "ellipse;fillColor=none;", 100, 200, 80, 80, NULL);
    shape(p, "dia", "rhombus;", 200, 200, 40, 40, NULL);
    shape(p, "data", "shape=parallelogram;fixedSize=1;size=10;", 250, 200, 60, 30, NULL);
    shape(p, "prep", "shape=hexagon;", 320, 200, 80, 30, NULL);
    shape(p, "tri", "triangle;", 250, 250, 30, 50, NULL);
    shape(p, "proc", "shape=process;html=1;", 300, 250, 100, 50, "Two<br>lines");
    shape(p, "words", "text;html=1;fontColor=#00ff00;fontStyle=1;fontFamily=Courier New;", 100, 290, 60, 10, "Mono");
    shape(p, "hid", "rounded=0;visible=0;", 100, 100, 10, 10, "Hidden");
    owf_dattrs_set(&owf_dpage_find(p, "hid")->extra, "visible", "0");
    e = owf_dpage_add(p, "e1", "1", OWF_DCELL_EDGE);
    owf_dcell_set(&e->source, "box");
    owf_dcell_set(&e->target, "round");
    owf_dcell_set(&e->style, "endArrow=classic;startArrow=classic;strokeWidth=2;");
    owf_dcell_set(&e->value, "link");
    e->geo.flags = OWF_DGEO_HAS | OWF_DGEO_RELATIVE;

    CHECK_N(owf_dpdf_write(&d, 0, &pdf, &n), OWF_OK);
    s = (const char *)pdf;
    CHECK(n > 8 && !memcmp(s, "%PDF-1.4", 8));
    CHECK_N(count(s, n, "/Type /Page "), 1);
    CHECK(find(s, n, "/MediaBox [0 0 240 165]") != NULL);
    CHECK(find(s, n, "/Title <FEFF0046006C006F0077>") != NULL);   /* the page name, "Flow" */
    CHECK(find(s, n, "1.000 0.000 0.000 rg") != NULL);   /* the red fill */
    CHECK(find(s, n, "0.000 0.000 1.000 RG") != NULL);   /* the blue line */
    CHECK(find(s, n, "(Start & go) Tj") != NULL);         /* an HTML label as its text */
    CHECK(find(s, n, "(Two) Tj") && find(s, n, "(lines) Tj"));   /* a <br> is two lines */
    CHECK(find(s, n, "(link) Tj") != NULL);               /* the connector's label */
    CHECK(find(s, n, "(Mono) Tj") != NULL);
    CHECK(find(s, n, "/Courier-Bold") != NULL);           /* fontFamily and fontStyle */
    CHECK(find(s, n, "0.000 1.000 0.000 rg") != NULL);   /* its green */
    CHECK(find(s, n, "(Hidden)") == NULL);                /* a hidden cell isn't drawn */
    CHECK(count(s, n, " d\n") + count(s, n, "] 0 d") >= 1);  /* a dash pattern for the dashed box */
    owf_free(pdf);

    CHECK_N(owf_dpdf_write(&d, 1, &pdf, &n), OWF_ERR_FORMAT);   /* no such page */
    CHECK(pdf == NULL && n == 0);
    owf_diagram_free(&d);
}

static void test_pages(void)
{
    owf_diagram d;
    owf_dpage *p;
    unsigned char *pdf = NULL;
    size_t n = 0;
    owf_diagram_init(&d);
    p = owf_diagram_add_page(&d, "a", "One");
    shape(p, "x", "rounded=0;", 0, 0, 50, 50, "First");
    p = owf_diagram_add_page(&d, "b", "Two");             /* an empty page: 100 pixels square and the margin */
    CHECK_N(owf_dpdf_write(&d, -1, &pdf, &n), OWF_OK);
    CHECK_N(count((const char *)pdf, n, "/Type /Page "), 2);
    CHECK(find((const char *)pdf, n, "/MediaBox [0 0 52.50 52.50]") != NULL);   /* 50 + 20 pixels */
    CHECK(find((const char *)pdf, n, "/MediaBox [0 0 90 90]") != NULL);         /* 100 + 20 pixels */
    owf_free(pdf);
    CHECK_N(owf_dpdf_write(&d, 1, &pdf, &n), OWF_OK);
    CHECK_N(count((const char *)pdf, n, "/Type /Page "), 1);
    CHECK(find((const char *)pdf, n, "(First)") == NULL);
    owf_free(pdf);
    owf_diagram_free(&d);

    owf_diagram_init(&d);                                 /* no pages at all */
    CHECK_N(owf_dpdf_write(&d, -1, &pdf, &n), OWF_ERR_FORMAT);
    owf_diagram_free(&d);
}

int main(void)
{
    test_shapes();
    test_pages();
    printf("diagram PDF: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
