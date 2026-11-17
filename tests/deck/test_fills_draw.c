/*
 * Hatched and see-through fills and shadows drawn (Presentation gap 1, part 3): the look carried to
 * GFOpenLayout's shape style (0.22), the hatch lines cut to an outline (opr_hatch_lines), and the PDF
 * and print: shadows behind, see-through fills through PDF 1.4's constant alpha (owf_pdf_alpha), hatch
 * lines inside. make fills-draw-test, make fills-draw-check (ASan).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "openpresentation_core.h"
#include "owf_pdf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks, failures;

#define CHECK(cond, what)                                                                                  \
    do {                                                                                                    \
        ++checks;                                                                                           \
        if (!(cond)) {                                                                                      \
            ++failures;                                                                                     \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, what);                                            \
        }                                                                                                   \
    } while (0)

static const char FODP[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
    "<office:document xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" "
    "xmlns:style=\"urn:oasis:names:tc:opendocument:xmlns:style:1.0\" "
    "xmlns:draw=\"urn:oasis:names:tc:opendocument:xmlns:drawing:1.0\" "
    "xmlns:svg=\"urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0\" "
    "xmlns:presentation=\"urn:oasis:names:tc:opendocument:xmlns:presentation:1.0\" "
    "office:version=\"1.2\" office:mimetype=\"application/vnd.oasis.opendocument.presentation\">"
    "<office:styles>"
    "<draw:hatch draw:name=\"across\" draw:style=\"single\" draw:color=\"#ff0000\" draw:distance=\"0.5cm\" "
    "draw:rotation=\"0\"/>"
    "<draw:hatch draw:name=\"grid\" draw:style=\"double\" draw:color=\"#0000ff\" draw:distance=\"1cm\" "
    "draw:rotation=\"0\"/>"
    "</office:styles>"
    "<office:automatic-styles>"
    /* yellow, 40% see-through, red lines across 5 mm apart, a half see-through dark shadow */
    "<style:style style:name=\"gr1\" style:family=\"graphic\"><style:graphic-properties draw:fill=\"hatch\" "
    "draw:fill-hatch-name=\"across\" draw:fill-hatch-solid=\"true\" draw:fill-color=\"#ffff00\" draw:opacity=\"60%\" "
    "draw:shadow=\"visible\" draw:shadow-offset-x=\"0.2cm\" draw:shadow-offset-y=\"0.2cm\" "
    "draw:shadow-color=\"#333333\" draw:shadow-opacity=\"50%\" draw:stroke=\"none\"/></style:style>"
    /* plain green: drawn as before, no graphics state */
    "<style:style style:name=\"gr2\" style:family=\"graphic\"><style:graphic-properties draw:fill=\"solid\" "
    "draw:fill-color=\"#00ff00\" draw:stroke=\"none\"/></style:style>"
    "<style:style style:name=\"dp1\" style:family=\"drawing-page\"><style:drawing-page-properties "
    "draw:fill=\"hatch\" draw:fill-hatch-name=\"grid\" draw:fill-hatch-solid=\"true\" draw:fill-color=\"#ccccff\" "
    "draw:opacity=\"80%\"/></style:style>"
    "</office:automatic-styles>"
    "<office:body><office:presentation><draw:page draw:name=\"one\" draw:style-name=\"dp1\">"
    "<draw:rect draw:style-name=\"gr1\" svg:x=\"2cm\" svg:y=\"2cm\" svg:width=\"4cm\" svg:height=\"2cm\"/>"
    "<draw:rect draw:style-name=\"gr2\" svg:x=\"8cm\" svg:y=\"2cm\" svg:width=\"4cm\" svg:height=\"2cm\"/>"
    "</draw:page></office:presentation></office:body></office:document>";

static size_t count(const unsigned char *data, size_t length, const char *what)
{
    size_t n = 0, k, w = strlen(what);
    for (k = 0; k + w <= length; k++)
        if (!memcmp(data + k, what, w)) n++;
    return n;
}

/* the lines cut to a square and to a triangle */
static void test_lines(void)
{
    static const int square[8] = {0, 0, 1000, 0, 1000, 1000, 0, 1000};
    static const int triangle[6] = {0, 0, 1000, 1000, 0, 1000};
    int out[4 * 64], n, k, ok;
    n = opr_hatch_lines(square, 4, 0, 0, OPR_HATCH_SINGLE, 0, 100, out, 64);
    CHECK(n == 10, "a square: ten lines across, 100 apart, from its top edge");
    for (k = 0, ok = 1; k < n; k++)
        if (out[k * 4] != 0 || out[k * 4 + 2] != 1000 || out[k * 4 + 1] != k * 100 || out[k * 4 + 3] != k * 100) ok = 0;
    CHECK(ok, "each from the left edge to the right one");
    CHECK(opr_hatch_lines(square, 4, 0, 0, OPR_HATCH_DOUBLE, 0, 100, out, 64) == 20, "double: and ten down");
    /* room for 5 of the 10: twice as far apart, every other line kept where it was */
    n = opr_hatch_lines(square, 4, 0, 0, OPR_HATCH_SINGLE, 0, 100, out, 5);
    for (k = 0, ok = n == 5; ok && k < n; k++)
        if (out[k * 4 + 1] != k * 200 || out[k * 4] != 0 || out[k * 4 + 2] != 1000) ok = 0;
    CHECK(ok, "too many for out: 200 apart instead, not none");
    n = opr_hatch_lines(square, 4, 0, 0, OPR_HATCH_DOUBLE, 0, 1, out, 64);
    CHECK(n > 0 && n <= 64, "a hairline apart on the square, double: widened until they fit");
    CHECK(opr_hatch_lines(square, 4, 0, 0, OPR_HATCH_SINGLE, 0, 100, out, 0) == -1, "no room at all: -1");
    CHECK(opr_hatch_lines(square, 4, 0, 0, OPR_HATCH_SINGLE, 0, 0, out, 64) == 0, "no distance: nothing");
    CHECK(opr_hatch_lines(square, 4, 0, 0, 0, 0, 100, out, 64) == 0, "not a hatch: nothing");
    n = opr_hatch_lines(square, 4, 50, 50, OPR_HATCH_SINGLE, 0, 100, out, 64);
    CHECK(n == 10 && out[1] == 50, "the pattern runs through the point given");
    n = opr_hatch_lines(triangle, 3, 0, 0, OPR_HATCH_SINGLE, 0, 250, out, 64);
    for (k = 0, ok = n == 4; ok && k < n; k++)
        if (out[k * 4] != 0 || out[k * 4 + 2] != k * 250 || out[k * 4 + 1] != k * 250) ok = 0;
    CHECK(ok, "a triangle: each line as far as the slope");
    n = opr_hatch_lines(square, 4, 0, 0, OPR_HATCH_SINGLE, 900, 100, out, 64);
    for (k = 0, ok = n == 10; ok && k < n; k++)
        if (out[k * 4] != out[k * 4 + 2] || out[k * 4] % 100 || out[k * 4] < 0 || out[k * 4] > 1000) ok = 0;
    CHECK(ok, "at 90 degrees: lines down");
    n = opr_hatch_lines(square, 4, 0, 0, OPR_HATCH_SINGLE, 450, 100, out, 64);
    CHECK(n >= 13 && n <= 15, "at 45 degrees: the diagonal's 1414 across, 100 apart");
    /* the sine is libowf's one (owf_fill_sin16k: 30 degrees is 8192, its cosine 14189, of 16384): each line
     * runs along (cos, -sin), y down, to within the rounding of its two ends */
    n = opr_hatch_lines(square, 4, 0, 0, OPR_HATCH_SINGLE, 300, 100, out, 64);
    for (k = 0, ok = n == 14 && out[4 + 1] == 115 && out[4 + 2] == 199; ok && k < n; k++) {
        long dx = out[k * 4 + 2] - out[k * 4], dy = out[k * 4 + 3] - out[k * 4 + 1];
        long off = dy * 14189 + dx * 8192;
        if (off < 0) off = -off;
        if (off > 2 * (14189 + 8192)) ok = 0;            /* the first only touches the corner: a point */
    }
    CHECK(ok, "at 30 degrees: every line on the one sine's slope");
}

static const ol_shape_style *shape_at(const opr_slide *laid, int frame)
{
    size_t i;
    for (i = 0; i < ol_display_count(laid->doc); i++) {
        const ol_display_op *op = ol_display_get(laid->doc, i);
        if (op->type == OL_OP_SHAPE && op->graphic && opr_slide_frame_of(laid, op) == frame)
            return &op->graphic->shape;
    }
    return NULL;
}

static void test_layout_and_pdf(void)
{
    owf_deck *deck = NULL;
    opr_slide *laid;
    unsigned char *pdf = NULL;
    size_t length = 0;
    CHECK(owf_import_deck_memory((const unsigned char *)FODP, sizeof FODP - 1, "fodp", &deck, NULL, NULL) == OWF_OK &&
          deck, "read");
    if (!deck) return;
    laid = opr_slide_layout(deck, 0, NULL, NULL, "1 January 2026");
    CHECK(laid != NULL, "laid out");
    if (laid) {
        const ol_shape_style *bg = shape_at(laid, 0), *a = NULL;
        int f;
        for (f = 1; f < laid->nframes && !a; f++)
            if ((a = shape_at(laid, f)) && !OL_SHAPE_STYLE_HAS_HATCH(a)) a = NULL;
        CHECK(bg && OL_SHAPE_STYLE_HAS_HATCH(bg) && bg->hatch == OL_HATCH_DOUBLE, "the background: a grid");
        /* see-through over white: cc to cc + (ff - cc) * 20% */
        CHECK(bg && bg->fill_argb == 0xffd6d6ffu && bg->hatch_argb == 0xff3333ffu,
              "the background's colours seen over white, opaque");
        CHECK(a && a->fill_argb == 0x99ffff00u, "the shape's yellow 60% opaque (alpha 153)");
        CHECK(a && a->hatch == OL_HATCH_SINGLE && a->hatch_argb == 0x99ff0000u &&
              a->hatch_distance == 283 * OL_UNITS_PER_TWIP, "its red lines, 5 mm (283 twips) apart, see-through too");
        CHECK(a && a->shadow_argb == 0x80333333u && a->shadow_dx == 113 * OL_UNITS_PER_TWIP &&
              a->shadow_dy == 113 * OL_UNITS_PER_TWIP, "its shadow");
        opr_slide_free(laid);
    }
    CHECK(opr_export_pdf(deck, NULL, &pdf, &length, NULL) == OWF_OK && pdf, "the PDF");
    if (pdf) {
        CHECK(count(pdf, length, "/GA50 gs") == 1, "the shadow half see-through, once");
        CHECK(count(pdf, length, "/GA60 gs") >= 2, "the fill and its lines 60% opaque");
        CHECK(count(pdf, length, "/ExtGState") == 1 && count(pdf, length, "/GA50 << /ca 0.50 /CA 0.50 >>") == 1 &&
              count(pdf, length, "/GA60 << /ca 0.60 /CA 0.60 >>") == 1, "the page names both");
        /* the background grid's lines 1 cm apart and the shape's 5 mm apart: a hairline each (0.5 pt) */
        CHECK(count(pdf, length, "0.50 w") > 20, "hatch lines drawn");
        CHECK(count(pdf, length, "0.200 0.200 0.200 rg") == 1, "the shadow's colour");
        CHECK(count(pdf, length, "0.000 1.000 0.000 rg") == 1, "the plain shape as before");
    }
    free(pdf);
    owf_deck_free(deck);
}

/* the page writer alone: a see-through rectangle, then a solid one, on one page */
static void test_pdf_alpha(void)
{
    owf_pdf *pdf = owf_pdf_new();
    unsigned char *data = NULL;
    size_t length = 0;
    CHECK(pdf && owf_pdf_page(pdf, 4000, 4000) == 0, "a page");
    owf_pdf_alpha(pdf, 25);
    owf_pdf_rect(pdf, 0, 0, 1000, 1000, OWF_SHADE(0xFF0000), 0, 0);
    owf_pdf_alpha(pdf, 100);
    owf_pdf_rect(pdf, 2000, 0, 1000, 1000, OWF_SHADE(0x0000FF), 0, 0);
    owf_pdf_alpha(pdf, 0);                     /* below 1: 1 */
    owf_pdf_rect(pdf, 0, 2000, 1000, 1000, OWF_SHADE(0x00FF00), 0, 0);
    owf_pdf_alpha(pdf, 100);
    CHECK(owf_pdf_finish(pdf, "a", &data, &length, NULL) == OWF_OK && data, "finished");
    if (data) {
        CHECK(count(data, length, " gs\n") == 2 && count(data, length, "/GA25 gs") == 1 && count(data, length, "/GA1 gs") == 1,
              "two see-through, one solid");
        CHECK(count(data, length, "/GA25 << /ca 0.25 /CA 0.25 >>") == 1 && count(data, length, "/GA1 << /ca 0.01 /CA 0.01 >>") == 1,
              "both in the page's resources");
    }
    free(data);
    owf_pdf_free(pdf);
}

/* a see-through gradient: its 48 overlapping bands go opaque into one transparency group, which the
 * page paints once at the alpha, so 50% stays 50% where bands overlap (not 48 layers of 50%) */
static void test_pdf_gradient_alpha(void)
{
    static const int box[8] = {0, 0, 2000, 0, 2000, 1000, 0, 1000};
    owf_pdf *pdf = owf_pdf_new();
    unsigned char *data = NULL;
    size_t length = 0;
    CHECK(pdf && owf_pdf_page(pdf, 4000, 4000) == 0, "a page");
    owf_pdf_alpha(pdf, 50);
    CHECK(owf_pdf_gradient(pdf, box, 4, 0, 0, 2000, 1000, OWF_PDF_GRADIENT_LINEAR, 0, OWF_SHADE(0xFF0000),
                           OWF_SHADE(0x0000FF)) == 0, "a linear gradient, half see-through");
    CHECK(owf_pdf_gradient(pdf, box, 4, 0, 0, 2000, 1000, OWF_PDF_GRADIENT_RADIAL, 0, OWF_SHADE(0xFF0000),
                           OWF_SHADE(0x0000FF)) == 0, "a radial one");
    owf_pdf_alpha(pdf, 100);
    CHECK(owf_pdf_gradient(pdf, box, 4, 0, 2000, 2000, 1000, OWF_PDF_GRADIENT_LINEAR, 0, OWF_SHADE(0xFF0000),
                           OWF_SHADE(0x0000FF)) == 0, "an opaque one, drawn on the page as before");
    CHECK(owf_pdf_finish(pdf, "g", &data, &length, NULL) == OWF_OK && data, "finished");
    if (data) {
        CHECK(count(data, length, " gs\n") == 2 && count(data, length, "/GA50 gs\n/Fm1 Do") == 1 &&
              count(data, length, "/GA50 gs\n/Fm2 Do") == 1, "the alpha set once for each gradient, not per band");
        CHECK(count(data, length, "/Subtype /Form") == 2 &&
              count(data, length, "/Group << /Type /Group /S /Transparency >>") == 2, "each a transparency group");
        CHECK(count(data, length, "/Fm1 ") == 2 && count(data, length, "/Fm2 ") == 2, "both in the page's resources");
        CHECK(count(data, length, " rg\n") == 48 * 3,
              "48 bands each, opaque, in the groups and on the page");
        CHECK(count(data, length, "/GA50 << /ca 0.50 /CA 0.50 >>") == 1, "the alpha named once");
    }
    free(data);
    owf_pdf_free(pdf);
}

/* Budgie's case on #18: pages come from realloc, so a page's own alphas start cleared; one see-through page
 * and twenty opaque ones (past the first growth of the page table) name /ExtGState once, on the first */
static void test_opaque_pages(void)
{
    owf_pdf *pdf = owf_pdf_new();
    unsigned char *data = NULL;
    size_t length = 0;
    int k;
    CHECK(pdf && owf_pdf_page(pdf, 4000, 4000) == 0, "a page");
    owf_pdf_alpha(pdf, 40);
    owf_pdf_rect(pdf, 0, 0, 1000, 1000, OWF_SHADE(0xFF0000), 0, 0);
    owf_pdf_alpha(pdf, 100);
    for (k = 0; k < 20; k++) {
        CHECK(owf_pdf_page(pdf, 4000, 4000) == 0, "an opaque page");
        owf_pdf_rect(pdf, 0, 0, 1000, 1000, OWF_SHADE(0x0000FF), 0, 0);
    }
    CHECK(owf_pdf_finish(pdf, "o", &data, &length, NULL) == OWF_OK && data, "finished");
    if (data)
        CHECK(count(data, length, "/ExtGState") == 1 && count(data, length, "/GA40 << /ca") == 1 &&
              count(data, length, " gs\n") == 1, "the opaque pages name no graphics states");
    free(data);
    owf_pdf_free(pdf);
}

int main(void)
{
    test_opaque_pages();
    test_lines();
    test_layout_and_pdf();
    test_pdf_alpha();
    test_pdf_gradient_alpha();
    printf("fills-draw: %d checks, %d failed\n", checks, failures);
    return failures != 0;
}
