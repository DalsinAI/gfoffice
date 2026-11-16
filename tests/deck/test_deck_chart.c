/*
 * test_deck_chart: a chart on a slide (Presentation gap 2, docs/parity/impress.md). A chart read from its chart
 * part on its own, with no book (owf_chart_from_part): its kind, title, legend, series and the values its caches
 * hold; copied and cleared; written as a chart part and read back the same; and a PPTX slide's chart read into
 * the deck as an OWF_OBJ_CHART that holds it, copied with its object.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "owf.h"
#include "owf_deck.h"
#include "owf_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks, failures;

#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
    printf(__VA_ARGS__); printf("\n"); } } while (0)

/* Columns of two series over three quarters, as PowerPoint writes a chart part (with an embedded workbook's
 * cells in <c:f>, and their caches). */
static const char chart_xml[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
    "<c:chartSpace xmlns:c=\"http://schemas.openxmlformats.org/drawingml/2006/chart\" "
    "xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
    "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">"
    "<c:chart><c:title><c:tx><c:rich><a:bodyPr/><a:p><a:r><a:t>Sales</a:t></a:r></a:p></c:rich></c:tx></c:title>"
    "<c:autoTitleDeleted val=\"0\"/><c:plotArea><c:layout/>"
    "<c:barChart><c:barDir val=\"col\"/><c:grouping val=\"clustered\"/><c:varyColors val=\"0\"/>"
    "<c:ser><c:idx val=\"0\"/><c:order val=\"0\"/>"
    "<c:tx><c:strRef><c:f>Sheet1!$B$1</c:f><c:strCache><c:ptCount val=\"1\"/><c:pt idx=\"0\"><c:v>North</c:v></c:pt>"
    "</c:strCache></c:strRef></c:tx>"
    "<c:cat><c:strRef><c:f>Sheet1!$A$2:$A$4</c:f><c:strCache><c:ptCount val=\"3\"/>"
    "<c:pt idx=\"0\"><c:v>Q1</c:v></c:pt><c:pt idx=\"1\"><c:v>Q2</c:v></c:pt><c:pt idx=\"2\"><c:v>Q3</c:v></c:pt>"
    "</c:strCache></c:strRef></c:cat>"
    "<c:val><c:numRef><c:f>Sheet1!$B$2:$B$4</c:f><c:numCache><c:formatCode>General</c:formatCode><c:ptCount val=\"3\"/>"
    "<c:pt idx=\"0\"><c:v>4.3</c:v></c:pt><c:pt idx=\"1\"><c:v>2.5</c:v></c:pt><c:pt idx=\"2\"><c:v>3.5</c:v></c:pt>"
    "</c:numCache></c:numRef></c:val></c:ser>"
    "<c:ser><c:idx val=\"1\"/><c:order val=\"1\"/>"
    "<c:tx><c:strRef><c:f>Sheet1!$C$1</c:f><c:strCache><c:ptCount val=\"1\"/><c:pt idx=\"0\"><c:v>South</c:v></c:pt>"
    "</c:strCache></c:strRef></c:tx>"
    "<c:cat><c:strRef><c:f>Sheet1!$A$2:$A$4</c:f><c:strCache><c:ptCount val=\"3\"/>"
    "<c:pt idx=\"0\"><c:v>Q1</c:v></c:pt><c:pt idx=\"1\"><c:v>Q2</c:v></c:pt><c:pt idx=\"2\"><c:v>Q3</c:v></c:pt>"
    "</c:strCache></c:strRef></c:cat>"
    "<c:val><c:numRef><c:f>Sheet1!$C$2:$C$4</c:f><c:numCache><c:formatCode>General</c:formatCode><c:ptCount val=\"3\"/>"
    "<c:pt idx=\"0\"><c:v>2.4</c:v></c:pt><c:pt idx=\"2\"><c:v>1.8</c:v></c:pt>"
    "</c:numCache></c:numRef></c:val></c:ser>"
    "<c:axId val=\"1\"/><c:axId val=\"2\"/></c:barChart>"
    "<c:catAx><c:axId val=\"1\"/><c:crossAx val=\"2\"/></c:catAx><c:valAx><c:axId val=\"2\"/><c:crossAx val=\"1\"/>"
    "<c:majorGridlines/></c:valAx></c:plotArea>"
    "<c:legend><c:legendPos val=\"b\"/></c:legend><c:plotVisOnly val=\"1\"/></c:chart></c:chartSpace>";

static int near(double a, double b) { return a - b < 1e-9 && b - a < 1e-9; }

/* The chart above, as read: columns, "Sales", the legend at the bottom, two series of three quarters. */
static void check_sales(const owf_chart *c, const char *what)
{
    CHECK(c->kind == OWF_CHART_COLUMN && c->grouping == OWF_CHART_CLUSTERED, "%s: kind %d %d", what, c->kind,
          c->grouping);
    CHECK(c->title && !strcmp(c->title, "Sales"), "%s: title [%s]", what, c->title ? c->title : "(none)");
    CHECK(c->legend == 'b', "%s: legend %d", what, c->legend);
    CHECK(c->nseries == 2, "%s: %d series", what, c->nseries);
    if (c->nseries != 2) return;
    CHECK(c->series[0].name && !strcmp(c->series[0].name, "North") && c->series[1].name &&
          !strcmp(c->series[1].name, "South"), "%s: series names", what);
    CHECK(c->series[0].npoints == 3 && c->series[1].npoints == 3, "%s: points %d %d", what, c->series[0].npoints,
          c->series[1].npoints);
    if (c->series[0].npoints != 3 || c->series[1].npoints != 3) return;
    CHECK(c->series[0].points[0].label && !strcmp(c->series[0].points[0].label, "Q1") && c->series[0].points[2].label &&
          !strcmp(c->series[0].points[2].label, "Q3"), "%s: categories", what);
    CHECK(c->series[0].points[0].has_value && near(c->series[0].points[0].value, 4.3) &&
          near(c->series[0].points[1].value, 2.5) && near(c->series[0].points[2].value, 3.5), "%s: North's values", what);
    CHECK(c->series[1].points[0].has_value && near(c->series[1].points[0].value, 2.4) && !c->series[1].points[1].has_value &&
          c->series[1].points[2].has_value && near(c->series[1].points[2].value, 1.8), "%s: South's values, a gap in Q2",
          what);
}

static void test_part(void)
{
    owf_chart c, d, e;
    owf_buf b;
    memset(&c, 0x5a, sizeof c);              /* from_part sets every field */
    CHECK(owf_chart_from_part(chart_xml, strlen(chart_xml), &c) == OWF_OK, "read");
    check_sales(&c, "read");
    CHECK(c.series[0].values && !strcmp(c.series[0].values, "Sheet1!$B$2:$B$4"), "the workbook's cells are kept");

    /* a copy is its own */
    memset(&d, 0, sizeof d);
    CHECK(owf_chart_copy(&d, &c) == OWF_OK, "copy");
    check_sales(&d, "copy");
    CHECK(d.title != c.title && d.series != c.series && d.series[0].points != c.series[0].points &&
          d.series[0].points[0].label != c.series[0].points[0].label, "the copy shares nothing");
    owf_chart_clear(&c);
    CHECK(c.nseries == 0 && c.series == NULL && c.title == NULL, "cleared");
    check_sales(&d, "copy after the first is cleared");

    /* written as a chart part and read back: the same chart, its values from the part's own caches */
    owf_buf_init(&b);
    CHECK(owf_chart_part_xml(&d, &b) == OWF_OK && b.length > 0, "written");
    memset(&e, 0, sizeof e);
    CHECK(owf_chart_from_part((const char *)b.data, b.length, &e) == OWF_OK, "read back");
    check_sales(&e, "read back");
    owf_buf_free(&b);
    owf_chart_clear(&e);

    /* the cells gone (a deck's chart on its own data): still written with its values, and read back */
    free(d.series[0].values);
    d.series[0].values = NULL;
    free(d.series[0].categories);
    d.series[0].categories = NULL;
    free(d.series[0].name_cell);
    d.series[0].name_cell = NULL;
    owf_buf_init(&b);
    CHECK(owf_chart_part_xml(&d, &b) == OWF_OK, "written without cells");
    memset(&e, 0, sizeof e);
    CHECK(owf_chart_from_part((const char *)b.data, b.length, &e) == OWF_OK, "read back without cells");
    check_sales(&e, "read back without cells");
    owf_buf_free(&b);
    owf_chart_clear(&e);
    owf_chart_clear(&d);

    /* not a chart */
    memset(&e, 0, sizeof e);
    CHECK(owf_chart_from_part("<a/>", 4, &e) == OWF_ERR_FORMAT && e.nseries == 0, "not a chart part");
    CHECK(owf_chart_from_part("<c:chartSpace", 13, &e) != OWF_OK && e.series == NULL, "damaged");
    CHECK(owf_chart_from_part(NULL, 0, &e) == OWF_ERR_FORMAT, "nothing");
    owf_chart_clear(&e);
    owf_chart_clear(NULL);
}

/* ---- A PPTX of one slide with the chart on it, made here ---- */

static const char ct_xml[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
    "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
    "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
    "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
    "<Override PartName=\"/ppt/presentation.xml\" "
    "ContentType=\"application/vnd.openxmlformats-officedocument.presentationml.presentation.main+xml\"/>"
    "<Override PartName=\"/ppt/slides/slide1.xml\" "
    "ContentType=\"application/vnd.openxmlformats-officedocument.presentationml.slide+xml\"/>"
    "<Override PartName=\"/ppt/charts/chart1.xml\" "
    "ContentType=\"application/vnd.openxmlformats-officedocument.drawingml.chart+xml\"/></Types>";
static const char root_rels[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
    "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
    "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/"
    "officeDocument\" Target=\"ppt/presentation.xml\"/></Relationships>";
static const char pres_xml[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
    "<p:presentation xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
    "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" "
    "xmlns:p=\"http://schemas.openxmlformats.org/presentationml/2006/main\">"
    "<p:sldMasterIdLst><p:sldMasterId id=\"2147483648\" r:id=\"rId1\"/></p:sldMasterIdLst>"
    "<p:sldIdLst><p:sldId id=\"256\" r:id=\"rId2\"/></p:sldIdLst>"
    "<p:sldSz cx=\"9144000\" cy=\"6858000\"/><p:notesSz cx=\"6858000\" cy=\"9144000\"/></p:presentation>";
static const char pres_rels[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
    "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
    "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/"
    "slideMaster\" Target=\"slideMasters/slideMaster1.xml\"/>"
    "<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/slide\" "
    "Target=\"slides/slide1.xml\"/></Relationships>";
/* the plainest master and layout PowerPoint reads: no placeholders, no theme */
static const char master_xml[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
    "<p:sldMaster xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
    "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" "
    "xmlns:p=\"http://schemas.openxmlformats.org/presentationml/2006/main\"><p:cSld><p:spTree>"
    "<p:nvGrpSpPr><p:cNvPr id=\"1\" name=\"\"/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr><p:grpSpPr/></p:spTree></p:cSld>"
    "<p:clrMap bg1=\"lt1\" tx1=\"dk1\" bg2=\"lt2\" tx2=\"dk2\" accent1=\"accent1\" accent2=\"accent2\" "
    "accent3=\"accent3\" accent4=\"accent4\" accent5=\"accent5\" accent6=\"accent6\" hlink=\"hlink\" "
    "folHlink=\"folHlink\"/><p:sldLayoutIdLst><p:sldLayoutId id=\"2147483649\" r:id=\"rId1\"/></p:sldLayoutIdLst>"
    "</p:sldMaster>";
static const char master_rels[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
    "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
    "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/"
    "slideLayout\" Target=\"../slideLayouts/slideLayout1.xml\"/></Relationships>";
static const char layout_xml[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
    "<p:sldLayout xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
    "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" "
    "xmlns:p=\"http://schemas.openxmlformats.org/presentationml/2006/main\" type=\"blank\"><p:cSld name=\"Blank\">"
    "<p:spTree><p:nvGrpSpPr><p:cNvPr id=\"1\" name=\"\"/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr><p:grpSpPr/>"
    "</p:spTree></p:cSld></p:sldLayout>";
static const char layout_rels[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
    "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
    "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/"
    "slideMaster\" Target=\"../slideMasters/slideMaster1.xml\"/></Relationships>";
static const char slide_xml[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
    "<p:sld xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
    "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" "
    "xmlns:p=\"http://schemas.openxmlformats.org/presentationml/2006/main\"><p:cSld><p:spTree>"
    "<p:nvGrpSpPr><p:cNvPr id=\"1\" name=\"\"/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr><p:grpSpPr/>"
    "<p:graphicFrame><p:nvGraphicFramePr><p:cNvPr id=\"4\" name=\"Chart 3\"/><p:cNvGraphicFramePr/><p:nvPr/>"
    "</p:nvGraphicFramePr><p:xfrm><a:off x=\"914400\" y=\"1371600\"/><a:ext cx=\"7315200\" cy=\"4572000\"/></p:xfrm>"
    "<a:graphic><a:graphicData uri=\"http://schemas.openxmlformats.org/drawingml/2006/chart\">"
    "<c:chart xmlns:c=\"http://schemas.openxmlformats.org/drawingml/2006/chart\" r:id=\"rId2\"/>"
    "</a:graphicData></a:graphic></p:graphicFrame></p:spTree></p:cSld></p:sld>";
static const char slide_rels[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
    "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
    "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/"
    "slideLayout\" Target=\"../slideLayouts/slideLayout1.xml\"/>"
    "<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/chart\" "
    "Target=\"../charts/chart1.xml\"/></Relationships>";

static unsigned char *make_pptx(size_t *n)
{
    owf_buf out;
    owf_zip *z;
    unsigned char *data;
    owf_buf_init(&out);
    if (!(z = owf_zip_new(&out))) return NULL;
    owf_zip_add(z, "[Content_Types].xml", ct_xml, strlen(ct_xml), 1);
    owf_zip_add(z, "_rels/.rels", root_rels, strlen(root_rels), 1);
    owf_zip_add(z, "ppt/presentation.xml", pres_xml, strlen(pres_xml), 1);
    owf_zip_add(z, "ppt/_rels/presentation.xml.rels", pres_rels, strlen(pres_rels), 1);
    owf_zip_add(z, "ppt/slideMasters/slideMaster1.xml", master_xml, strlen(master_xml), 1);
    owf_zip_add(z, "ppt/slideMasters/_rels/slideMaster1.xml.rels", master_rels, strlen(master_rels), 1);
    owf_zip_add(z, "ppt/slideLayouts/slideLayout1.xml", layout_xml, strlen(layout_xml), 1);
    owf_zip_add(z, "ppt/slideLayouts/_rels/slideLayout1.xml.rels", layout_rels, strlen(layout_rels), 1);
    owf_zip_add(z, "ppt/slides/slide1.xml", slide_xml, strlen(slide_xml), 1);
    owf_zip_add(z, "ppt/slides/_rels/slide1.xml.rels", slide_rels, strlen(slide_rels), 1);
    owf_zip_add(z, "ppt/charts/chart1.xml", chart_xml, strlen(chart_xml), 1);
    if (owf_zip_finish(z) != OWF_OK || out.failed) { owf_buf_free(&out); return NULL; }
    data = out.data;
    *n = out.length;
    return data;
}

static void test_pptx_slide(void)
{
    size_t n = 0;
    unsigned char *data = make_pptx(&n);
    owf_deck *deck = NULL;
    const owf_obj *o = NULL;
    owf_obj copy;
    int i;
    CHECK(data != NULL, "the PPTX is made");
    if (!data) return;
    CHECK(owf_import_deck_memory(data, n, "pptx", &deck, NULL, NULL) == OWF_OK && deck, "read the PPTX");
    free(data);
    if (!deck) return;
    CHECK(deck->nslides == 1, "%d slides", deck->nslides);
    for (i = 0; deck->nslides && i < deck->slides[0].nobjs; i++)
        if (deck->slides[0].objs[i].kind == OWF_OBJ_CHART) o = &deck->slides[0].objs[i];
    CHECK(o != NULL, "the frame is a chart");
    if (o) {
        CHECK(o->chart != NULL, "it holds its chart");
        if (o->chart) check_sales(o->chart, "on the slide");
        CHECK(o->x == 1440 && o->y == 2160 && o->width == 11520 && o->height == 7200, "its box %d %d %d %d", o->x, o->y,
              o->width, o->height);
        CHECK(o->name && !strcmp(o->name, "Chart 3"), "its name");
        memset(&copy, 0, sizeof copy);
        CHECK(owf_obj_copy(&copy, o) == 0, "copied");
        CHECK(copy.chart != NULL && copy.chart != o->chart, "the copy has a chart of its own");
        if (copy.chart) check_sales(copy.chart, "copied");
        owf_obj_clear(&copy);
    }
    owf_deck_free(deck);
}

int main(void)
{
    test_part();
    test_pptx_slide();
    printf("test_deck_chart: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
