/*
 * GFOpenDiag's Style window's sums on the PC (src/diag/odg_style.c, opendiag_style.h): what the selection shares,
 * the typed text checked, and every selected cell set as one Undo step. Run under ASan and UBSan by make check.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <stdio.h>
#include <string.h>
#include "opendiag_style.h"

static int fails, checks;

#define CHECK(cond) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_N(got, want) do { long g_ = (long)(got), w_ = (long)(want); checks++; if (g_ != w_) { fails++; \
    printf("FAIL %s:%d: %s is %ld, want %ld\n", __FILE__, __LINE__, #got, g_, w_); } } while (0)
#define CHECK_S(got, want) do { const char *g_ = (got), *w_ = (want); checks++; if (!g_ || strcmp(g_, w_)) { fails++; \
    printf("FAIL %s:%d: %s is \"%s\", want \"%s\"\n", __FILE__, __LINE__, #got, g_ ? g_ : "(null)", w_); } } while (0)
#define PX(n) OWF_DNUM(n)

static owf_diagram dg;
static odg_doc doc;

static void fresh(void)
{
    owf_diagram_init(&dg);
    owf_diagram_add_page(&dg, "p", "Page-1");
    odg_init(&doc, &dg, 0);
}

static void done(void)
{
    odg_free(&doc);
    owf_diagram_free(&dg);
}

static const char *style_of(const char *id)
{
    const owf_dcell *c = owf_dpage_find(odg_page(&doc), id);
    return c && c->style ? c->style : "";
}

static void test_names(void)
{
    CHECK_S(odg_prop_key(ODG_PROP_FILL), "fillColor");
    CHECK_S(odg_prop_key(ODG_PROP_FONT_SIZE), "fontSize");
    CHECK_S(odg_prop_title(ODG_PROP_STROKE), "Line");
    CHECK_S(odg_prop_key(ODG_NPROPS), "");
    CHECK_N(odg_prop_kind_of(ODG_PROP_FONT_COLOUR), ODG_KIND_COLOUR);
    CHECK_N(odg_prop_kind_of(ODG_PROP_STROKE_WIDTH), ODG_KIND_NUMBER);
    CHECK_N(odg_prop_kind_of(ODG_PROP_ROUNDED), ODG_KIND_SWITCH);
}

static void test_check(void)
{
    char out[24];
    CHECK(odg_prop_check(ODG_PROP_FILL, "#DAE8FC", out, sizeof out)); CHECK_S(out, "#dae8fc");
    CHECK(odg_prop_check(ODG_PROP_FILL, " #f0A ", out, sizeof out)); CHECK_S(out, "#ff00aa");
    CHECK(odg_prop_check(ODG_PROP_STROKE, "None", out, sizeof out)); CHECK_S(out, "none");
    CHECK(odg_prop_check(ODG_PROP_STROKE, "", out, sizeof out)); CHECK_S(out, "");
    CHECK(odg_prop_check(ODG_PROP_STROKE, "   ", out, sizeof out)); CHECK_S(out, "");
    CHECK(!odg_prop_check(ODG_PROP_FILL, "#ggg", out, sizeof out));
    CHECK(!odg_prop_check(ODG_PROP_FILL, "#12345", out, sizeof out));
    CHECK(!odg_prop_check(ODG_PROP_FILL, "red", out, sizeof out));
    CHECK(!odg_prop_check(ODG_PROP_FILL, "#fff;x=1", out, sizeof out));   /* nothing can slip into the style */
    CHECK(!odg_prop_check(ODG_PROP_FILL, "nones", out, sizeof out));
    CHECK(odg_prop_check(ODG_PROP_FONT_SIZE, "12", out, sizeof out)); CHECK_S(out, "12");
    CHECK(odg_prop_check(ODG_PROP_FONT_SIZE, "12.50", out, sizeof out)); CHECK_S(out, "12.5");
    CHECK(odg_prop_check(ODG_PROP_STROKE_WIDTH, "0.25", out, sizeof out)); CHECK_S(out, "0.25");
    CHECK(odg_prop_check(ODG_PROP_STROKE_WIDTH, "2.000", out, sizeof out)); CHECK_S(out, "2");
    CHECK(odg_prop_check(ODG_PROP_STROKE_WIDTH, ".5", out, sizeof out)); CHECK_S(out, "0.5");
    CHECK(odg_prop_check(ODG_PROP_FONT_SIZE, "999", out, sizeof out)); CHECK_S(out, "999");
    CHECK(!odg_prop_check(ODG_PROP_FONT_SIZE, "1000", out, sizeof out));
    CHECK(!odg_prop_check(ODG_PROP_FONT_SIZE, "99999999999999999999", out, sizeof out));
    CHECK(!odg_prop_check(ODG_PROP_FONT_SIZE, "0", out, sizeof out));
    CHECK(!odg_prop_check(ODG_PROP_FONT_SIZE, "0.00", out, sizeof out));
    CHECK(!odg_prop_check(ODG_PROP_FONT_SIZE, "-3", out, sizeof out));
    CHECK(!odg_prop_check(ODG_PROP_FONT_SIZE, "1.2.3", out, sizeof out));
    CHECK(!odg_prop_check(ODG_PROP_FONT_SIZE, "1.234", out, sizeof out));
    CHECK(!odg_prop_check(ODG_PROP_FONT_SIZE, ".", out, sizeof out));
    CHECK(!odg_prop_check(ODG_PROP_FONT_SIZE, "12px", out, sizeof out));
    CHECK(odg_prop_check(ODG_PROP_DASHED, "1", out, sizeof out)); CHECK_S(out, "1");
    CHECK(odg_prop_check(ODG_PROP_DASHED, "0", out, sizeof out)); CHECK_S(out, "0");
    CHECK(!odg_prop_check(ODG_PROP_DASHED, "yes", out, sizeof out));
    CHECK(!odg_prop_check(ODG_NPROPS, "1", out, sizeof out));
    CHECK(!odg_prop_check(ODG_PROP_FILL, NULL, out, sizeof out));
}

static void test_common_and_apply(void)
{
    char a[24], b[24], e[24], out[64];
    const char *ids[3], *gone[1] = { "nope" };
    owf_dpoint p0 = { PX(0), PX(100) }, p1 = { PX(50), PX(100) };
    fresh();
    odg_add_shape(&doc, PX(10), PX(10), PX(40), PX(20), "rounded=1;whiteSpace=wrap;fillColor=#dae8fc;fontSize=14;", NULL, a);
    odg_add_shape(&doc, PX(100), PX(10), PX(40), PX(20), "ellipse;fillColor=#dae8fc;", NULL, b);
    odg_add_edge(&doc, NULL, NULL, &p0, &p1, "dashed=1;", e);
    ids[0] = a; ids[1] = b; ids[2] = e;
    CHECK(odg_style_common(&doc, ids, 2, ODG_PROP_FILL, out, sizeof out));
    CHECK_S(out, "#dae8fc");
    CHECK(!odg_style_common(&doc, ids, 2, ODG_PROP_FONT_SIZE, out, sizeof out));  /* 14 and none: they differ */
    CHECK_S(out, "");
    CHECK(odg_style_common(&doc, ids, 2, ODG_PROP_STROKE, out, sizeof out));      /* neither sets it */
    CHECK_S(out, "");
    CHECK(odg_style_common(&doc, ids, 1, ODG_PROP_ROUNDED, out, sizeof out));
    CHECK_S(out, "1");
    CHECK(odg_style_common(&doc, ids + 1, 1, ODG_PROP_ROUNDED, out, sizeof out));
    CHECK_S(out, "0");
    CHECK(odg_style_common(&doc, ids + 2, 1, ODG_PROP_DASHED, out, sizeof out));
    CHECK_S(out, "1");
    CHECK(!odg_style_common(&doc, gone, 1, ODG_PROP_FILL, out, sizeof out));      /* nothing on the page */

    /* set on all three as one step */
    CHECK_N(odg_style_apply(&doc, ids, 3, ODG_PROP_STROKE, "#F00"), OWF_OK);
    CHECK_S(style_of(a), "rounded=1;whiteSpace=wrap;fillColor=#dae8fc;fontSize=14;strokeColor=#ff0000;");
    CHECK(strstr(style_of(b), "strokeColor=#ff0000") != NULL);
    CHECK(strstr(style_of(e), "strokeColor=#ff0000") != NULL);
    CHECK(odg_style_common(&doc, ids, 3, ODG_PROP_STROKE, out, sizeof out));
    CHECK_S(out, "#ff0000");
    CHECK_N(doc.nundo, 4);
    CHECK_N(odg_undo(&doc), OWF_OK);
    CHECK(strstr(style_of(a), "strokeColor") == NULL);
    CHECK(strstr(style_of(e), "strokeColor") == NULL);

    CHECK_N(odg_style_apply(&doc, ids, 2, ODG_PROP_FONT_SIZE, "16"), OWF_OK);
    CHECK(odg_style_common(&doc, ids, 2, ODG_PROP_FONT_SIZE, out, sizeof out));
    CHECK_S(out, "16");
    CHECK_N(odg_style_apply(&doc, ids, 2, ODG_PROP_FONT_SIZE, ""), OWF_OK);      /* "" takes it off */
    CHECK(strstr(style_of(a), "fontSize") == NULL);
    CHECK_N(odg_style_apply(&doc, ids, 1, ODG_PROP_ROUNDED, "0"), OWF_OK);       /* a switch off: no key */
    CHECK(strstr(style_of(a), "rounded") == NULL);
    CHECK_N(odg_style_apply(&doc, ids + 1, 1, ODG_PROP_DASHED, "1"), OWF_OK);
    CHECK(strstr(style_of(b), "dashed=1") != NULL);

    /* refused: bad text, no cell; nothing changes and no Undo step is taken */
    {
        int steps = doc.nundo;
        CHECK_N(odg_style_apply(&doc, ids, 3, ODG_PROP_FILL, "blue"), OWF_ERR_FORMAT);
        CHECK_N(odg_style_apply(&doc, ids, 3, ODG_PROP_FILL, "#fff;shape=x"), OWF_ERR_FORMAT);
        CHECK_N(odg_style_apply(&doc, gone, 1, ODG_PROP_FILL, "#fff"), OWF_ERR_FORMAT);
        CHECK_N(odg_style_apply(&doc, ids, 3, ODG_NPROPS, "1"), OWF_ERR_FORMAT);
        CHECK_N(doc.nundo, steps);
        CHECK(strstr(style_of(a), "fillColor=#dae8fc") != NULL);
    }
    done();
}

/* The window's field starts with a value it can hold, or empty; a fill it can't hold is left as it is. */
static void test_field(void)
{
    char a[24], b[24], out[24];
    const char *ids[2];
    fresh();
    odg_add_shape(&doc, PX(10), PX(10), PX(40), PX(20), "fillColor=light-dark(#ffffff,#121212);fontSize=14;strokeColor=#ABC;", NULL, a);
    odg_add_shape(&doc, PX(100), PX(10), PX(40), PX(20), "fillColor=#dae8fc;fontSize=14;", NULL, b);
    ids[0] = a; ids[1] = b;
    CHECK(!odg_style_field(&doc, ids, 1, ODG_PROP_FILL, out, sizeof out));     /* light-dark(...): not held */
    CHECK_S(out, "");
    CHECK(odg_style_field(&doc, ids, 1, ODG_PROP_STROKE, out, sizeof out));    /* as the check writes it */
    CHECK_S(out, "#aabbcc");
    CHECK(odg_style_field(&doc, ids, 2, ODG_PROP_FONT_SIZE, out, sizeof out));
    CHECK_S(out, "14");
    CHECK(!odg_style_field(&doc, ids, 2, ODG_PROP_FILL, out, sizeof out));     /* they differ */
    CHECK_S(out, "");
    CHECK(!odg_style_field(&doc, ids + 1, 1, ODG_PROP_STROKE, out, sizeof out)); /* not set */
    CHECK_S(out, "");
    CHECK(strstr(style_of(a), "fillColor=light-dark(#ffffff,#121212)") != NULL);  /* and nothing written */
    done();
}

int main(void)
{
    test_names();
    test_check();
    test_common_and_apply();
    test_field();
    printf("diagram style: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
