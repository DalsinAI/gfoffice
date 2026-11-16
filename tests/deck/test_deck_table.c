/*
 * test_deck_table: a slide's table (Presentation gap 3, docs/parity/impress.md). Its rows' heights, its merged
 * cells and its design (header row, banded rows, the style's name) read from PPTX and from ODP into the model;
 * copied with its object and freed with it.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "owf.h"
#include "owf_deck.h"
#include "owf_internal.h"
#include "owf_deck_int.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks, failures;

#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
    printf(__VA_ARGS__); printf("\n"); } } while (0)

#define NS_A "xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
#define NS_R "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" "
#define NS_P "xmlns:p=\"http://schemas.openxmlformats.org/presentationml/2006/main\""
#define REL "http://schemas.openxmlformats.org/officeDocument/2006/relationships/"
#define XML "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"

static const char ct_xml[] = XML
    "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
    "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
    "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
    "<Override PartName=\"/ppt/presentation.xml\" "
    "ContentType=\"application/vnd.openxmlformats-officedocument.presentationml.presentation.main+xml\"/>"
    "<Override PartName=\"/ppt/slides/slide1.xml\" "
    "ContentType=\"application/vnd.openxmlformats-officedocument.presentationml.slide+xml\"/></Types>";
static const char root_rels[] = XML
    "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
    "<Relationship Id=\"rId1\" Type=\"" REL "officeDocument\" Target=\"ppt/presentation.xml\"/></Relationships>";
static const char pres_xml[] = XML
    "<p:presentation " NS_A NS_R NS_P ">"
    "<p:sldMasterIdLst><p:sldMasterId id=\"2147483648\" r:id=\"rId1\"/></p:sldMasterIdLst>"
    "<p:sldIdLst><p:sldId id=\"256\" r:id=\"rId2\"/></p:sldIdLst>"
    "<p:sldSz cx=\"9144000\" cy=\"6858000\"/><p:notesSz cx=\"6858000\" cy=\"9144000\"/></p:presentation>";
static const char pres_rels[] = XML
    "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
    "<Relationship Id=\"rId1\" Type=\"" REL "slideMaster\" Target=\"slideMasters/slideMaster1.xml\"/>"
    "<Relationship Id=\"rId2\" Type=\"" REL "slide\" Target=\"slides/slide1.xml\"/></Relationships>";
static const char master_xml[] = XML
    "<p:sldMaster " NS_A NS_R NS_P "><p:cSld><p:spTree>"
    "<p:nvGrpSpPr><p:cNvPr id=\"1\" name=\"\"/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr><p:grpSpPr/></p:spTree></p:cSld>"
    "<p:clrMap bg1=\"lt1\" tx1=\"dk1\" bg2=\"lt2\" tx2=\"dk2\" accent1=\"accent1\" accent2=\"accent2\" "
    "accent3=\"accent3\" accent4=\"accent4\" accent5=\"accent5\" accent6=\"accent6\" hlink=\"hlink\" "
    "folHlink=\"folHlink\"/><p:sldLayoutIdLst><p:sldLayoutId id=\"2147483649\" r:id=\"rId1\"/></p:sldLayoutIdLst>"
    "</p:sldMaster>";
static const char master_rels[] = XML
    "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
    "<Relationship Id=\"rId1\" Type=\"" REL "slideLayout\" Target=\"../slideLayouts/slideLayout1.xml\"/>"
    "</Relationships>";
static const char layout_xml[] = XML
    "<p:sldLayout " NS_A NS_R NS_P " type=\"blank\"><p:cSld name=\"Blank\">"
    "<p:spTree><p:nvGrpSpPr><p:cNvPr id=\"1\" name=\"\"/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr><p:grpSpPr/>"
    "</p:spTree></p:cSld></p:sldLayout>";
static const char layout_rels[] = XML
    "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
    "<Relationship Id=\"rId1\" Type=\"" REL "slideMaster\" Target=\"../slideMasters/slideMaster1.xml\"/>"
    "</Relationships>";

#define TC(text, attrs) "<a:tc" attrs "><a:txBody><a:bodyPr/><a:p><a:r><a:t>" text "</a:t></a:r></a:p></a:txBody><a:tcPr/></a:tc>"
#define TC_EMPTY(attrs) "<a:tc" attrs "><a:txBody><a:bodyPr/><a:p/></a:txBody><a:tcPr/></a:tc>"

/* three columns, three rows: "Region" across A1:B1, "North" down A2:A3; rows 0.4 in, 0.8 in and 0.4 in;
 * PowerPoint's Medium Style 2 - Accent 1 with a header row and banded rows */
static const char slide_xml[] = XML
    "<p:sld " NS_A NS_R NS_P "><p:cSld><p:spTree>"
    "<p:nvGrpSpPr><p:cNvPr id=\"1\" name=\"\"/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr><p:grpSpPr/>"
    "<p:graphicFrame><p:nvGraphicFramePr><p:cNvPr id=\"4\" name=\"Table 3\"/><p:cNvGraphicFramePr/><p:nvPr/>"
    "</p:nvGraphicFramePr><p:xfrm><a:off x=\"914400\" y=\"1371600\"/><a:ext cx=\"5486400\" cy=\"1463040\"/></p:xfrm>"
    "<a:graphic><a:graphicData uri=\"http://schemas.openxmlformats.org/drawingml/2006/table\">"
    "<a:tbl><a:tblPr firstRow=\"1\" bandRow=\"1\"><a:tableStyleId>{5C22544A-7EE6-4342-B048-85BDC9FD1C3A}</a:tableStyleId>"
    "</a:tblPr><a:tblGrid><a:gridCol w=\"1828800\"/><a:gridCol w=\"1828800\"/><a:gridCol w=\"1828800\"/></a:tblGrid>"
    "<a:tr h=\"365760\">" TC("Region", " gridSpan=\"2\"") TC_EMPTY(" hMerge=\"1\"") TC("Total", "") "</a:tr>"
    "<a:tr h=\"731520\">" TC("North", " rowSpan=\"2\"") TC("Q1", "") TC("4", "") "</a:tr>"
    "<a:tr h=\"365760\">" TC_EMPTY(" vMerge=\"1\"") TC("Q2", "") TC("5", "") "</a:tr>"
    "</a:tbl></a:graphicData></a:graphic></p:graphicFrame></p:spTree></p:cSld></p:sld>";
static const char slide_rels[] = XML
    "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
    "<Relationship Id=\"rId1\" Type=\"" REL "slideLayout\" Target=\"../slideLayouts/slideLayout1.xml\"/>"
    "</Relationships>";

static unsigned char *make_pptx(size_t *n)
{
    owf_buf out;
    owf_zip *z;
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
    if (owf_zip_finish(z) != OWF_OK || out.failed) { owf_buf_free(&out); return NULL; }
    *n = out.length;
    return out.data;
}

/* the same table as ODF writes it: spans on the first cell, covered cells after; row styles; the template */
#define ODC(text, attrs) "<table:table-cell" attrs "><text:p>" text "</text:p></table:table-cell>"
static const char fodp[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<office:document xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" "
    "xmlns:style=\"urn:oasis:names:tc:opendocument:xmlns:style:1.0\" "
    "xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\" "
    "xmlns:table=\"urn:oasis:names:tc:opendocument:xmlns:table:1.0\" "
    "xmlns:draw=\"urn:oasis:names:tc:opendocument:xmlns:drawing:1.0\" "
    "xmlns:fo=\"urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0\" "
    "xmlns:presentation=\"urn:oasis:names:tc:opendocument:xmlns:presentation:1.0\" "
    "xmlns:svg=\"urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0\" office:version=\"1.3\" "
    "office:mimetype=\"application/vnd.oasis.opendocument.presentation\">"
    "<office:automatic-styles>"
    "<style:style style:name=\"co1\" style:family=\"table-column\"><style:table-column-properties "
    "style:column-width=\"5.08cm\"/></style:style>"
    "<style:style style:name=\"ro1\" style:family=\"table-row\"><style:table-row-properties "
    "style:row-height=\"1.016cm\"/></style:style>"
    "<style:style style:name=\"ro2\" style:family=\"table-row\"><style:table-row-properties "
    "style:min-row-height=\"2.032cm\"/></style:style>"
    "</office:automatic-styles>"
    "<office:body><office:presentation><draw:page draw:name=\"page1\">"
    "<draw:frame svg:width=\"15.24cm\" svg:height=\"4.064cm\" svg:x=\"2.54cm\" svg:y=\"3.81cm\">"
    "<table:table table:template-name=\"blue\" table:use-first-row-styles=\"true\" "
    "table:use-banding-rows-styles=\"true\" table:use-last-column-styles=\"true\">"
    "<table:table-column table:style-name=\"co1\" table:number-columns-repeated=\"3\"/>"
    "<table:table-row table:style-name=\"ro1\">" ODC("Region", " table:number-columns-spanned=\"2\"")
    "<table:covered-table-cell/>" ODC("Total", "") "</table:table-row>"
    "<table:table-row table:style-name=\"ro2\">" ODC("North", " table:number-rows-spanned=\"2\"") ODC("Q1", "")
    ODC("4", "") "</table:table-row>"
    "<table:table-row>" "<table:covered-table-cell/>" ODC("Q2", "") ODC("5", "") "</table:table-row>"
    "</table:table></draw:frame></draw:page></office:presentation></office:body></office:document>";

static const owf_obj *table_obj(const owf_deck *d)
{
    int i;
    for (i = 0; d && d->nslides && i < d->slides[0].nobjs; i++)
        if (d->slides[0].objs[i].kind == OWF_OBJ_TABLE) return &d->slides[0].objs[i];
    return NULL;
}

static const owf_table_merge *merge_at(const owf_obj *o, int row, int col)
{
    int i;
    for (i = 0; o && i < o->nmerges; i++)
        if (o->merges[i].row == row && o->merges[i].col == col) return &o->merges[i];
    return NULL;
}

static int near(int got, int want)
{
    return got >= want - 2 && got <= want + 2;
}

/* What both files say: 3 x 3, the two merges, the heights in twips (0.4 in is 576). */
static void check_table(const owf_obj *o, const char *what, int last_height)
{
    const owf_table_merge *m;
    CHECK(o && o->ncols == 3, "%s: three columns", what);
    if (!o) return;
    CHECK(o->nrows == 3 && o->row_heights, "%s: three rows' heights (%d)", what, o->nrows);
    if (o->nrows == 3 && o->row_heights)
        CHECK(near(o->row_heights[0], 576) && near(o->row_heights[1], 1152) && near(o->row_heights[2], last_height),
              "%s: the heights %d %d %d", what, o->row_heights[0], o->row_heights[1], o->row_heights[2]);
    CHECK(o->nmerges == 2, "%s: two merged cells (%d)", what, o->nmerges);
    m = merge_at(o, 0, 0);
    CHECK(m && m->rows == 1 && m->cols == 2, "%s: Region across two columns", what);
    m = merge_at(o, 1, 0);
    CHECK(m && m->rows == 2 && m->cols == 1, "%s: North down two rows", what);
    CHECK(!merge_at(o, 0, 1) && !merge_at(o, 2, 0), "%s: no merge starts in a covered cell", what);
    CHECK(o->table_design & OWF_TABLE_FIRST_ROW, "%s: a header row", what);
    CHECK(o->table_design & OWF_TABLE_BANDED_ROWS, "%s: banded rows", what);
    CHECK(!(o->table_design & (OWF_TABLE_FIRST_COL | OWF_TABLE_LAST_ROW | OWF_TABLE_BANDED_COLS)),
          "%s: nothing else on but what the file says (%#x)", what, o->table_design);
}

static void test_pptx(void)
{
    size_t n = 0;
    unsigned char *data = make_pptx(&n);
    owf_deck *deck = NULL;
    const owf_obj *o;
    owf_obj copy;
    CHECK(data && owf_import_deck_memory(data, n, "pptx", &deck, NULL, NULL) == OWF_OK && deck, "the PPTX read");
    free(data);
    o = table_obj(deck);
    check_table(o, "PPTX", 576);
    CHECK(o && !(o->table_design & OWF_TABLE_LAST_COL), "PPTX: no last column");
    CHECK(o && o->table_style && !strcmp(o->table_style, "{5C22544A-7EE6-4342-B048-85BDC9FD1C3A}"),
          "PPTX: the style's id kept");
    if (o) {
        memset(&copy, 0, sizeof copy);
        CHECK(owf_obj_copy(&copy, o) == 0, "copied");
        check_table(&copy, "the copy", 576);
        CHECK(copy.row_heights != o->row_heights && copy.merges != o->merges && copy.table_style != o->table_style,
              "the copy has its own");
        owf_obj_clear(&copy);
        CHECK(!copy.row_heights && !copy.merges && !copy.table_style && !copy.nmerges && !copy.nrows, "cleared");
    }
    owf_deck_free(deck);
}

static void test_odp(void)
{
    owf_deck *deck = NULL;
    const owf_obj *o;
    CHECK(owf_import_deck_memory((const unsigned char *)fodp, strlen(fodp), "fodp", &deck, NULL, NULL) == OWF_OK &&
          deck, "the FODP read");
    o = table_obj(deck);
    check_table(o, "ODP", 0);         /* the third row has no style: as its text needs */
    CHECK(o && (o->table_design & OWF_TABLE_LAST_COL), "ODP: the last column");
    CHECK(o && o->table_style && !strcmp(o->table_style, "blue"), "ODP: the template's name");
    owf_deck_free(deck);
}

int main(void)
{
    test_pptx();
    test_odp();
    printf("test_deck_table: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
