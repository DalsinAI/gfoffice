/*
 * Gradient fills on slides (gfoffice gap 2.8): ODP's draw:gradient and PPTX's a:gradFill read into
 * owf_look, saved back to both, carried to OpenLayout's shape style and drawn into the PDF as bands.
 * The files are written here. make gradient-test, make gradient-check (ASan).
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

static const char FODP_HEAD[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
    "<office:document xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" "
    "xmlns:style=\"urn:oasis:names:tc:opendocument:xmlns:style:1.0\" "
    "xmlns:draw=\"urn:oasis:names:tc:opendocument:xmlns:drawing:1.0\" "
    "xmlns:svg=\"urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0\" "
    "xmlns:fo=\"urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0\" "
    "xmlns:presentation=\"urn:oasis:names:tc:opendocument:xmlns:presentation:1.0\" "
    "xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\" "
    "office:version=\"1.2\" office:mimetype=\"application/vnd.oasis.opendocument.presentation\">"
    "<office:styles>"
    "<draw:gradient draw:name=\"down\" draw:style=\"linear\" draw:angle=\"0\" draw:start-color=\"#0000ff\" "
    "draw:end-color=\"#ff0000\" draw:border=\"0%\"/>"
    "<draw:gradient draw:name=\"slant\" draw:style=\"axial\" draw:angle=\"45deg\" draw:start-color=\"#00ff00\" "
    "draw:end-color=\"#000000\"/>"
    "<draw:gradient draw:name=\"glow\" draw:style=\"radial\" draw:cx=\"50%\" draw:cy=\"50%\" "
    "draw:start-color=\"#000080\" draw:end-color=\"#ffff00\"/>"
    "</office:styles>"
    "<office:automatic-styles>"
    "<style:style style:name=\"gr1\" style:family=\"graphic\"><style:graphic-properties draw:fill=\"gradient\" "
    "draw:fill-gradient-name=\"down\" draw:fill-color=\"#0000ff\" draw:stroke=\"none\"/></style:style>"
    "<style:style style:name=\"gr2\" style:family=\"graphic\"><style:graphic-properties draw:fill=\"gradient\" "
    "draw:fill-gradient-name=\"slant\" draw:stroke=\"none\"/></style:style>"
    "<style:style style:name=\"gr3\" style:family=\"graphic\"><style:graphic-properties draw:fill=\"gradient\" "
    "draw:fill-gradient-name=\"glow\" draw:stroke=\"none\"/></style:style>"
    "<style:style style:name=\"gr4\" style:family=\"graphic\"><style:graphic-properties draw:fill=\"gradient\" "
    "draw:fill-gradient-name=\"missing\" draw:fill-color=\"#123456\" draw:stroke=\"none\"/></style:style>"
    "</office:automatic-styles>"
    "<office:body><office:presentation><draw:page draw:name=\"one\">"
    "<draw:rect draw:style-name=\"gr1\" svg:x=\"1cm\" svg:y=\"1cm\" svg:width=\"4cm\" svg:height=\"2cm\"/>"
    "<draw:rect draw:style-name=\"gr2\" svg:x=\"6cm\" svg:y=\"1cm\" svg:width=\"4cm\" svg:height=\"2cm\"/>"
    "<draw:ellipse draw:style-name=\"gr3\" svg:x=\"1cm\" svg:y=\"4cm\" svg:width=\"4cm\" svg:height=\"4cm\"/>"
    "<draw:rect draw:style-name=\"gr4\" svg:x=\"6cm\" svg:y=\"4cm\" svg:width=\"4cm\" svg:height=\"2cm\"/>"
    "</draw:page></office:presentation></office:body></office:document>";

static const owf_obj *nth_shape(const owf_deck *d, int n)
{
    int i;
    if (!d || !d->nslides) return NULL;
    for (i = 0; i < d->slides[0].nobjs; i++)
        if (d->slides[0].objs[i].kind == OWF_OBJ_SHAPE && !n--) return &d->slides[0].objs[i];
    return NULL;
}

/* the four shapes as the file has them: linear down (ours 2700), axial at 45 degrees (linear, ours
 * 3150), radial (yellow centre, navy edge), and a missing gradient (its fill colour, plain) */
static void check_four(const owf_deck *d, const char *where)
{
    const owf_obj *a = nth_shape(d, 0), *b = nth_shape(d, 1), *c = nth_shape(d, 2), *e = nth_shape(d, 3);
    char what[160];
    snprintf(what, sizeof what, "%s: four shapes", where);
    CHECK(a && b && c && e, what);
    if (!a || !b || !c || !e) return;
    snprintf(what, sizeof what, "%s: linear, top to bottom, blue to red", where);
    CHECK(a->look.gradient == OWF_GRADIENT_LINEAR && a->look.gradient_angle == 2700 &&
          a->look.fill == OWF_SHADE(0x0000FF) && a->look.fill2 == OWF_SHADE(0xFF0000), what);
    snprintf(what, sizeof what, "%s: axial at 45 degrees, as linear", where);
    CHECK(b->look.gradient == OWF_GRADIENT_LINEAR && b->look.gradient_angle == 3150 &&
          b->look.fill == OWF_SHADE(0x00FF00) && b->look.fill2 == OWF_SHADE(0x000000), what);
    snprintf(what, sizeof what, "%s: radial, yellow at the centre, navy at the edge", where);
    CHECK(c->look.gradient == OWF_GRADIENT_RADIAL && c->look.fill == OWF_SHADE(0xFFFF00) &&
          c->look.fill2 == OWF_SHADE(0x000080), what);
    snprintf(what, sizeof what, "%s: a gradient not there: its fill colour, plain", where);
    CHECK(e->look.gradient == OWF_GRADIENT_NONE && e->look.fill == OWF_SHADE(0x123456), what);
}

static size_t count(const unsigned char *data, size_t length, const char *what)
{
    size_t n = 0, k, w = strlen(what);
    for (k = 0; k + w <= length; k++)
        if (!memcmp(data + k, what, w)) n++;
    return n;
}

static void test_odp(owf_deck **out)
{
    owf_deck *deck = NULL, *again = NULL;
    owf_report *report = owf_report_new();
    const owf_deck_format *odp = owf_deck_format_named("odp"), *used = NULL;
    unsigned char *data = NULL;
    size_t length = 0;
    CHECK(owf_import_deck_memory((const unsigned char *)FODP_HEAD, sizeof FODP_HEAD - 1, "fodp", &deck, report,
                                 &used) == OWF_OK, "the flat ODP read");
    check_four(deck, "ODP");
    {
        int k, found = 0;
        for (k = 0; report && k < owf_report_count(report); k++)
            if (strstr(owf_report_note(report, k, NULL, NULL), "Axial gradients")) found = 1;
        CHECK(found, "the axial one reported");
    }
    CHECK(odp && odp->export, "ODP saves");
    if (deck && odp && odp->export) {
        CHECK(odp->export(deck, &data, &length, NULL) == OWF_OK, "saved as ODP");
        CHECK(owf_import_deck_memory(data, length, "odp", &again, NULL, NULL) == OWF_OK, "the ODP read back");
        check_four(again, "ODP saved and read");
    }
    free(data);
    owf_deck_free(again);
    owf_report_free(report);
    *out = deck;
}

static void test_pptx(const owf_deck *deck)
{
    owf_deck *again = NULL;
    unsigned char *data = NULL;
    size_t length = 0;
    CHECK(opr_export_pptx(deck, &data, &length, NULL) == OWF_OK, "saved as PPTX");
    CHECK(data && count(data, length, "gradFill") == 0, "(zipped: the parts are deflated)");
    CHECK(owf_import_deck_memory(data, length, "pptx", &again, NULL, NULL) == OWF_OK, "the PPTX read back");
    check_four(again, "PPTX saved and read");
    free(data);
    owf_deck_free(again);
}

static void test_layout_and_pdf(const owf_deck *deck)
{
    opr_slide *laid = opr_slide_layout(deck, 0, NULL, NULL, "1 January 2026");
    unsigned char *pdf = NULL;
    size_t length = 0, i;
    int linear = 0, radial = 0;
    CHECK(laid != NULL, "laid out");
    if (laid) {
        for (i = 0; i < ol_display_count(laid->doc); i++) {
            const ol_display_op *op = ol_display_get(laid->doc, i);
            if (op->type != OL_OP_SHAPE || !op->graphic || !OL_SHAPE_STYLE_HAS_GRADIENT(&op->graphic->shape))
                continue;
            if (op->graphic->shape.gradient == OL_GRADIENT_LINEAR && op->graphic->shape.fill_argb == 0xFF0000FFu &&
                op->graphic->shape.fill2_argb == 0xFFFF0000u && op->graphic->shape.gradient_angle_tenths == 2700)
                linear++;
            if (op->graphic->shape.gradient == OL_GRADIENT_RADIAL) radial++;
        }
        CHECK(linear == 1 && radial == 1, "the shapes' gradients reach OpenLayout");
        opr_slide_free(laid);
    }
    CHECK(opr_export_pdf(deck, NULL, &pdf, &length, NULL) == OWF_OK && pdf, "the PDF");
    if (pdf) {
        /* three gradients clipped to their outlines, 48 bands each; the first band of the blue to red one is
         * 1/96 of the way from blue, its last 95/96 (each part truncated to a whole 0..255) */
        CHECK(count(pdf, length, "h W n") == 3, "three gradients, each clipped to its shape");
        CHECK(count(pdf, length, "0.007 0.000 0.992 rg") == 1 && count(pdf, length, "0.988 0.000 0.011 rg") == 1,
              "the linear one runs blue to red");
        CHECK(count(pdf, length, "0.070 0.203 0.337 rg") == 1, "the missing gradient: plain");
    }
    free(pdf);
}

/* the PDF writer alone: bands follow the angle; a radial one draws rings */
static void test_pdf_writer(void)
{
    owf_pdf *pdf = owf_pdf_new();
    unsigned char *data = NULL;
    size_t length = 0;
    int box[8] = {0, 0, 2000, 0, 2000, 1000, 0, 1000};
    CHECK(pdf && owf_pdf_page(pdf, 4000, 4000) == 0, "a page");
    CHECK(owf_pdf_gradient(pdf, box, 4, 0, 0, 2000, 1000, OWF_PDF_GRADIENT_LINEAR, 0, OWF_SHADE(0x000000),
                           OWF_SHADE(0xFFFFFF)) == 0, "a linear gradient");
    CHECK(owf_pdf_gradient(pdf, box, 4, 0, 0, 2000, 1000, OWF_PDF_GRADIENT_RADIAL, 0, OWF_SHADE(0xFFFFFF),
                           OWF_SHADE(0x000000)) == 0, "a radial gradient");
    CHECK(owf_pdf_gradient(pdf, box, 2, 0, 0, 2000, 1000, OWF_PDF_GRADIENT_LINEAR, 0, OWF_SHADE(0),
                           OWF_SHADE(0xFFFFFF)) == 0, "not an outline: nothing");
    CHECK(owf_pdf_finish(pdf, "g", &data, &length, NULL) == OWF_OK && data, "finished");
    if (data) {
        /* left to right, black first; the radial one from its dark corners in to a nearly white centre */
        CHECK(count(data, length, "h W n") == 2, "two clips");
        CHECK(count(data, length, " c h ") == 48, "48 rings");
        CHECK(count(data, length, "0.007 0.007 0.007 rg") == 1, "the linear one starts nearly black");
        CHECK(count(data, length, "0.992 0.992 0.992 rg") == 1, "the radial one's centre ring nearly white");
    }
    free(data);
    owf_pdf_free(pdf);
}

int main(void)
{
    owf_deck *deck = NULL;
    test_odp(&deck);
    if (deck) {
        test_pptx(deck);
        test_layout_and_pdf(deck);
    }
    owf_deck_free(deck);
    test_pdf_writer();
    printf("gradient: %d checks, %d failed\n", checks, failures);
    return failures != 0;
}
