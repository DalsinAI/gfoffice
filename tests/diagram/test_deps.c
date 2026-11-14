/*
 * The EPS writer (owf_deps_write, libowf/diagram/owf_eps.c drawn by owf_dpdf.c) on the PC: the DSC header with the
 * page's BoundingBox and name, the faces used re-encoded to WinAnsiEncoding, each shape filled and stroked with its
 * colours and dashes, labels as text with brackets and non-ASCII escaped, a blank page, and no such page. Run under
 * ASan and UBSan by make diagram-check.
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

static void test_eps(void)
{
    owf_diagram d;
    owf_dpage *p;
    owf_dcell *e;
    unsigned char *eps = NULL;
    size_t n = 0;
    const char *s;
    owf_diagram_init(&d);
    p = owf_diagram_add_page(&d, "p1", "Flow (one)");
    /* from 100,100 to 400,300: a page 300 + 20 by 200 + 20 pixels, 240 by 165 points */
    shape(p, "box", "rounded=0;whiteSpace=wrap;html=1;fillColor=#ff0000;", 100, 100, 120, 60, "Start &amp; (go)");
    shape(p, "round", "rounded=1;arcSize=50;html=1;dashed=1;strokeColor=#0000ff;", 280, 100, 120, 40, NULL);
    shape(p, "oval", "ellipse;fillColor=none;", 100, 200, 80, 80, NULL);
    shape(p, "dia", "rhombus;", 200, 200, 40, 40, "Caf\xC3\xA9 \xE2\x82\xAC \xE2\x86\x92");
    shape(p, "proc", "shape=process;html=1;", 300, 250, 100, 50, "Two<br>lines");
    shape(p, "words", "text;html=1;fontColor=#00ff00;fontStyle=1;fontFamily=Courier New;", 100, 290, 60, 10, "Mono");
    e = owf_dpage_add(p, "e1", "1", OWF_DCELL_EDGE);
    owf_dcell_set(&e->source, "box");
    owf_dcell_set(&e->target, "round");
    owf_dcell_set(&e->style, "endArrow=classic;strokeWidth=2;");
    owf_dcell_set(&e->value, "link");
    e->geo.flags = OWF_DGEO_HAS | OWF_DGEO_RELATIVE;

    CHECK_N(owf_deps_write(&d, 0, &eps, &n), OWF_OK);
    s = (const char *)eps;
    CHECK(n > 23 && !memcmp(s, "%!PS-Adobe-3.0 EPSF-3.0\n", 24));
    CHECK(find(s, n, "\n%%BoundingBox: 0 0 240 165\n") != NULL);
    CHECK(find(s, n, "\n%%HiResBoundingBox: 0 0 240 165\n") != NULL);
    CHECK(find(s, n, "\n%%Title: Flow ?one?\n") != NULL);          /* no brackets in a DSC line */
    CHECK(find(s, n, "\n%%Pages: 1\n") && find(s, n, "\n%%Page: 1 1\n"));
    CHECK(find(s, n, "%%DocumentNeededResources: font Helvetica\n") != NULL);
    CHECK(find(s, n, "%%+ font Courier-Bold\n") != NULL);
    CHECK(find(s, n, "/OW-Helvetica /Helvetica OW-font") && find(s, n, "/OW-Courier-Bold /Courier-Bold OW-font"));
    CHECK(find(s, n, "/OW-Times-Roman") == NULL);                  /* only the faces used */
    CHECK(find(s, n, "OW-WinAnsi 128 /Euro put") != NULL);
    CHECK(find(s, n, "1.000 0.000 0.000 setrgbcolor\nfill") != NULL);    /* the red fill */
    CHECK(find(s, n, "0.000 0.000 1.000 setrgbcolor") != NULL);          /* the blue line */
    CHECK(find(s, n, "] 0 setdash stroke") != NULL);                     /* the dashed box */
    CHECK(find(s, n, "curveto") != NULL);                                /* rounded corners and the oval */
    CHECK(find(s, n, "(Start & \\(go\\)) show") != NULL);               /* brackets escaped */
    CHECK(find(s, n, "(Caf\\351 \\200 ->) show") != NULL);              /* e acute, the euro, an arrow's fallback */
    CHECK(find(s, n, "(Two) show") && find(s, n, "(lines) show"));
    CHECK(find(s, n, "(link) show") && find(s, n, "(Mono) show"));
    CHECK(find(s, n, "0.000 1.000 0.000 setrgbcolor") != NULL);          /* the green text */
    CHECK_N(count(s, n, "gsave"), count(s, n, "grestore"));
    CHECK(n > 25 && !memcmp(s + n - 25, "showpage\n%%Trailer\n%%EOF\n", 25));
    CHECK(s[n] == 0 && strlen(s) == n);                                  /* text, NUL after */
    {                                    /* left for a PostScript interpreter to look at (build/host/test-deps.eps) */
        FILE *f = fopen("build/host/test-deps.eps", "wb");
        if (f) {
            fwrite(eps, 1, n, f);
            fclose(f);
        }
    }
    owf_free(eps);

    CHECK_N(owf_deps_write(&d, 1, &eps, &n), OWF_ERR_FORMAT);   /* no such page */
    CHECK(eps == NULL && n == 0);
    CHECK_N(owf_deps_write(&d, -1, &eps, &n), OWF_ERR_FORMAT);  /* EPS is one page */
    owf_diagram_free(&d);

    owf_diagram_init(&d);                                       /* an empty page: 100 pixels square and the margin */
    owf_diagram_add_page(&d, "b", "");
    CHECK_N(owf_deps_write(&d, 0, &eps, &n), OWF_OK);
    s = (const char *)eps;
    CHECK(find(s, n, "%%BoundingBox: 0 0 90 90\n") != NULL);
    CHECK(find(s, n, "%%Title: Diagram\n") != NULL);
    CHECK(find(s, n, "%%DocumentNeededResources") == NULL);     /* no text, no fonts */
    CHECK(find(s, n, "OW-WinAnsi") == NULL);
    owf_free(eps);
    owf_diagram_free(&d);
}

int main(void)
{
    test_eps();
    printf("diagram EPS: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
