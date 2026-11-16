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

#include "chart_sales.h"

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

/* A cell of the embedded workbook's first sheet: its text, or its number printed, "" when empty. */
static const char *cell_at(const owf_book *book, unsigned row, unsigned col)
{
    static char buf[64];
    const owf_cell *c = book && book->nsheets ? owf_sheet_find(&book->sheets[0], row, col) : NULL;
    if (!c || c->type == OWF_VALUE_EMPTY) return "";
    if (c->type == OWF_VALUE_TEXT) return c->text ? c->text : "";
    if (c->type == OWF_VALUE_NUMBER) { snprintf(buf, sizeof buf, "%g", c->number); return buf; }
    return "?";
}

/* A slide's chart as PowerPoint keeps it (owf_chart_pptx_parts): its part, its series pointing at the cells of
 * the workbook embedded with it, and that workbook: categories down column A, a series a column from B, the
 * names in row 1, a gap an empty cell. */
static void test_embedded(void)
{
    owf_chart c, e;
    owf_buf part, xlsx;
    owf_book *book = NULL;
    memset(&c, 0, sizeof c);
    CHECK(owf_chart_from_part(chart_xml, strlen(chart_xml), &c) == OWF_OK, "read for embedding");
    /* as a deck's chart holds it: no cells of its own */
    free(c.series[0].values); c.series[0].values = NULL;
    free(c.series[1].categories); c.series[1].categories = NULL;
    owf_buf_init(&part);
    owf_buf_init(&xlsx);
    CHECK(owf_chart_pptx_parts(&c, "rId1", &part, &xlsx) == OWF_OK && part.length && xlsx.length, "the parts");
    CHECK(c.series[0].values == NULL, "the chart given is not changed");
    owf_buf_putc(&part, 0);
    CHECK(part.data && strstr((const char *)part.data, "<c:externalData r:id=\"rId1\">"), "the part names its data");
    CHECK(part.data && strstr((const char *)part.data, "<c:f>Sheet1!$C$2:$C$4</c:f>") &&
          strstr((const char *)part.data, "<c:f>Sheet1!$A$2:$A$4</c:f>") &&
          strstr((const char *)part.data, "<c:f>Sheet1!$B$1</c:f>"), "its series point at the workbook's cells");
    memset(&e, 0, sizeof e);
    CHECK(part.data && owf_chart_from_part((const char *)part.data, part.length - 1, &e) == OWF_OK, "the part read back");
    check_sales(&e, "embedded, read back");
    CHECK(e.nseries == 2 && e.series[1].values && !strcmp(e.series[1].values, "Sheet1!$C$2:$C$4"), "its cells read back");
    owf_chart_clear(&e);

    CHECK(owf_import_book_memory(xlsx.data, xlsx.length, "xlsx", &book, NULL, NULL) == OWF_OK && book, "the workbook reads");
    if (book) {
        CHECK(book->nsheets == 1 && book->sheets[0].name && !strcmp(book->sheets[0].name, "Sheet1"), "one sheet, Sheet1");
        CHECK(!strcmp(cell_at(book, 0, 0), ""), "A1 empty [%s]", cell_at(book, 0, 0));
        CHECK(!strcmp(cell_at(book, 0, 1), "North") && !strcmp(cell_at(book, 0, 2), "South"), "the names in row 1");
        CHECK(!strcmp(cell_at(book, 1, 0), "Q1") && !strcmp(cell_at(book, 3, 0), "Q3"), "the categories down A");
        CHECK(!strcmp(cell_at(book, 1, 1), "4.3") && !strcmp(cell_at(book, 3, 1), "3.5"), "North's values in B [%s]",
              cell_at(book, 1, 1));
        CHECK(!strcmp(cell_at(book, 1, 2), "2.4") && !strcmp(cell_at(book, 2, 2), "") &&
              !strcmp(cell_at(book, 3, 2), "1.8"), "South's in C, its gap empty");
        owf_book_free(book);
    }
    owf_buf_free(&part);
    owf_buf_free(&xlsx);

    /* a name with markup in it: still well-formed, still read */
    free(c.series[0].name);
    c.series[0].name = malloc(16);
    if (c.series[0].name) strcpy(c.series[0].name, "A&B <x>");
    owf_buf_init(&part);
    owf_buf_init(&xlsx);
    CHECK(owf_chart_pptx_parts(&c, "rId1", &part, &xlsx) == OWF_OK, "markup in a name");
    book = NULL;
    CHECK(owf_import_book_memory(xlsx.data, xlsx.length, "xlsx", &book, NULL, NULL) == OWF_OK && book &&
          !strcmp(cell_at(book, 0, 1), "A&B <x>"), "the name in the workbook");
    if (book) owf_book_free(book);
    memset(&e, 0, sizeof e);
    CHECK(owf_chart_from_part((const char *)part.data, part.length, &e) == OWF_OK && e.nseries == 2 &&
          e.series[0].name && !strcmp(e.series[0].name, "A&B <x>"), "the name in the part");
    owf_chart_clear(&e);
    owf_buf_free(&part);
    owf_buf_free(&xlsx);
    owf_chart_clear(&c);
}

int main(void)
{
    test_part();
    test_pptx_slide();
    test_embedded();
    printf("test_deck_chart: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
