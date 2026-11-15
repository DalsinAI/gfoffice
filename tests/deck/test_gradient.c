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

/* a slide whose page style has a radial gradient background, white at the centre to teal at the edge */
static const char FODP_BG[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
    "<office:document xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" "
    "xmlns:style=\"urn:oasis:names:tc:opendocument:xmlns:style:1.0\" "
    "xmlns:draw=\"urn:oasis:names:tc:opendocument:xmlns:drawing:1.0\" "
    "xmlns:svg=\"urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0\" "
    "xmlns:presentation=\"urn:oasis:names:tc:opendocument:xmlns:presentation:1.0\" "
    "office:version=\"1.2\" office:mimetype=\"application/vnd.oasis.opendocument.presentation\">"
    "<office:styles><draw:gradient draw:name=\"sky\" draw:style=\"radial\" draw:start-color=\"#008080\" "
    "draw:end-color=\"#ffffff\"/></office:styles>"
    "<office:automatic-styles><style:style style:name=\"dp1\" style:family=\"drawing-page\">"
    "<style:drawing-page-properties draw:fill=\"gradient\" draw:fill-gradient-name=\"sky\"/></style:style>"
    "</office:automatic-styles>"
    "<office:body><office:presentation><draw:page draw:name=\"one\" draw:style-name=\"dp1\"/>"
    "</office:presentation></office:body></office:document>";

static void check_bg(const owf_deck *d, const char *where)
{
    char what[160];
    snprintf(what, sizeof what, "%s: a radial background, white centre, teal edge", where);
    CHECK(d && d->nslides == 1 && d->slides[0].background_gradient == OWF_GRADIENT_RADIAL &&
          d->slides[0].background == OWF_SHADE(0xFFFFFF) && d->slides[0].background2 == OWF_SHADE(0x008080), what);
}

static void test_background(void)
{
    owf_deck *deck = NULL, *again = NULL;
    const owf_deck_format *odp = owf_deck_format_named("odp");
    unsigned char *data = NULL;
    size_t length = 0, i;
    opr_slide *laid;
    int found = 0;
    CHECK(owf_import_deck_memory((const unsigned char *)FODP_BG, sizeof FODP_BG - 1, "fodp", &deck, NULL, NULL) ==
          OWF_OK, "the background ODP read");
    check_bg(deck, "ODP");
    if (!deck) return;
    CHECK(odp && odp->export(deck, &data, &length, NULL) == OWF_OK, "saved as ODP");
    CHECK(owf_import_deck_memory(data, length, "odp", &again, NULL, NULL) == OWF_OK, "read back");
    check_bg(again, "ODP saved and read");
    free(data); data = NULL;
    owf_deck_free(again); again = NULL;
    CHECK(opr_export_pptx(deck, &data, &length, NULL) == OWF_OK, "saved as PPTX");
    CHECK(owf_import_deck_memory(data, length, "pptx", &again, NULL, NULL) == OWF_OK, "read back");
    check_bg(again, "PPTX saved and read");
    free(data); data = NULL;
    owf_deck_free(again);
    laid = opr_slide_layout(deck, 0, NULL, NULL, "1 January 2026");
    if (laid) {
        for (i = 0; i < ol_display_count(laid->doc); i++) {
            const ol_display_op *op = ol_display_get(laid->doc, i);
            if (op->type == OL_OP_SHAPE && op->graphic && opr_slide_frame_of(laid, op) == 0 &&
                op->graphic->shape.gradient == OL_GRADIENT_RADIAL && op->graphic->shape.stroke_argb == 0)
                found++;
        }
        opr_slide_free(laid);
    }
    CHECK(found == 1, "the background frame carries the gradient, with no outline");
    CHECK(opr_export_pdf(deck, NULL, &data, &length, NULL) == OWF_OK && data, "the PDF");
    if (data) CHECK(count(data, length, "h W n") == 1 && count(data, length, " c h ") == 48, "the background's 48 rings");
    free(data);
    owf_deck_free(deck);
}

/* whether the pixel centre (px + 0.5, py + 0.5) is inside the polygon, even-odd, doubled to stay whole */
static int inside(const int *xy, int n, int px, int py)
{
    int i, in = 0;
    long sx = px * 2 + 1, sy = py * 2 + 1;
    for (i = 0; i < n; i++) {
        long x0 = xy[i * 2] * 2L, y0 = xy[i * 2 + 1] * 2L, x1 = xy[((i + 1) % n) * 2] * 2L,
             y1 = xy[((i + 1) % n) * 2 + 1] * 2L;
        if ((y0 > sy) != (y1 > sy) && sx < x0 + (sy - y0) * (x1 - x0) / (y1 - y0)) in = !in;
    }
    return in;
}

/* the screen's bands (opr_gradient_band) painted in order onto a 40 x 20 grid */
static void paint_bands(const int *outline, int n, int kind, int angle, unsigned long grid[20][40])
{
    int band, k, x, y, pts[2 * (OL_MAX_SHAPE_POINTS + 40)];
    unsigned long colour;
    for (y = 0; y < 20; y++)
        for (x = 0; x < 40; x++) grid[y][x] = 0x1000000UL;          /* nothing */
    for (band = 0; band < 16; band++) {
        k = opr_gradient_band(outline, n, 0, 0, 40, 20, kind, angle, 0x000000, 0xFFFFFF, band, 16, pts,
                              OL_MAX_SHAPE_POINTS + 40, &colour);
        CHECK(k >= 0, "a band");
        for (y = 0; y < 20; y++)
            for (x = 0; x < 40; x++)
                if (k >= 3 && inside(pts, k, x, y)) grid[y][x] = colour;
    }
}

static void test_screen_bands(void)
{
    static const int box[8] = {0, 0, 40, 0, 40, 20, 0, 20};
    static const int tri[6] = {0, 20, 20, 0, 40, 20};
    unsigned long grid[20][40];
    int x, y, covered = 1, rising = 1, outside_empty = 1, pts[16];
    unsigned long colour;
    paint_bands(box, 4, OPR_GRADIENT_LINEAR, 0, grid);
    for (y = 0; y < 20; y++)
        for (x = 0; x < 40; x++) {
            if (grid[y][x] & 0x1000000UL) covered = 0;
            if (x && grid[y][x] < grid[y][x - 1]) rising = 0;
        }
    CHECK(covered, "linear bands cover the whole box, no gaps");
    CHECK(rising, "left to right: darker to lighter");
    CHECK(grid[10][0] < 0x101010 && grid[10][39] > 0xEFEFEF, "black at the left, white at the right");
    paint_bands(box, 4, OPR_GRADIENT_LINEAR, 900, grid);
    CHECK(grid[19][20] < 0x101010 && grid[0][20] > 0xEFEFEF, "900: from the bottom up");
    paint_bands(tri, 3, OPR_GRADIENT_LINEAR, 0, grid);
    for (y = 0; y < 20; y++)
        for (x = 0; x < 40; x++)
            if (!inside(tri, 3, x, y) && !(grid[y][x] & 0x1000000UL)) outside_empty = 0;
    CHECK(outside_empty, "bands stay inside a triangle's outline");
    CHECK(!(grid[18][20] & 0x1000000UL), "and fill it");
    paint_bands(box, 4, OPR_GRADIENT_RADIAL, 0, grid);
    covered = 1;
    for (y = 0; y < 20; y++)
        for (x = 0; x < 40; x++)
            if (grid[y][x] & 0x1000000UL) covered = 0;
    CHECK(covered, "radial rings cover the whole box");
    CHECK(grid[10][20] < 0x202020 && grid[0][0] > 0xD0D0D0, "radial: dark centre, light corners");
    CHECK(opr_gradient_band(box, 4, 0, 0, 40, 20, OPR_GRADIENT_LINEAR, 0, 0, 0xFFFFFF, 0, 16, pts, 2, &colour) == -1,
          "too little room: -1");
    CHECK(opr_gradient_band(box, 2, 0, 0, 40, 20, OPR_GRADIENT_LINEAR, 0, 0, 0xFFFFFF, 0, 16, pts, 8, &colour) == 0,
          "not an outline: nothing");
}

/* a 128-point star (radii 3000 and 300, Lisette's probe): every band of either kind fits, none is lost */
static void test_star_bands(void)
{
    static const int star[2 * OL_MAX_SHAPE_POINTS] = {
        6000, 3000, 3300, 3015, 5986, 3294, 3297, 3044, 5942, 3585, 3291, 3073, 5871, 3871, 3282, 3101,
        5772, 4148, 3271, 3128, 5646, 4414, 3257, 3154, 5494, 4667, 3241, 3179, 5319, 4903, 3222, 3201,
        5121, 5121, 3201, 3222, 4903, 5319, 3179, 3241, 4667, 5494, 3154, 3257, 4414, 5646, 3128, 3271,
        4148, 5772, 3101, 3282, 3871, 5871, 3073, 3291, 3585, 5942, 3044, 3297, 3294, 5986, 3015, 3300,
        3000, 6000, 2985, 3300, 2706, 5986, 2956, 3297, 2415, 5942, 2927, 3291, 2129, 5871, 2899, 3282,
        1852, 5772, 2872, 3271, 1586, 5646, 2846, 3257, 1333, 5494, 2821, 3241, 1097, 5319, 2799, 3222,
        879, 5121, 2778, 3201, 681, 4903, 2759, 3179, 506, 4667, 2743, 3154, 354, 4414, 2729, 3128,
        228, 4148, 2718, 3101, 129, 3871, 2709, 3073, 58, 3585, 2703, 3044, 14, 3294, 2700, 3015,
        0, 3000, 2700, 2985, 14, 2706, 2703, 2956, 58, 2415, 2709, 2927, 129, 2129, 2718, 2899,
        228, 1852, 2729, 2872, 354, 1586, 2743, 2846, 506, 1333, 2759, 2821, 681, 1097, 2778, 2799,
        879, 879, 2799, 2778, 1097, 681, 2821, 2759, 1333, 506, 2846, 2743, 1586, 354, 2872, 2729,
        1852, 228, 2899, 2718, 2129, 129, 2927, 2709, 2415, 58, 2956, 2703, 2706, 14, 2985, 2700,
        3000, 0, 3015, 2700, 3294, 14, 3044, 2703, 3585, 58, 3073, 2709, 3871, 129, 3101, 2718,
        4148, 228, 3128, 2729, 4414, 354, 3154, 2743, 4667, 506, 3179, 2759, 4903, 681, 3201, 2778,
        5121, 879, 3222, 2799, 5319, 1097, 3241, 2821, 5494, 1333, 3257, 2846, 5646, 1586, 3271, 2872,
        5772, 1852, 3282, 2899, 5871, 2129, 3291, 2927, 5942, 2415, 3297, 2956, 5986, 2706, 3300, 2985,
    };
    int out[2 * OPR_GRADIENT_MAX_POINTS], band, kind, lost = 0, drawn = 0;
    unsigned long colour;
    for (kind = OPR_GRADIENT_LINEAR; kind <= OPR_GRADIENT_RADIAL; kind++)
        for (band = 0; band < 32; band++) {
            int m = opr_gradient_band(star, OL_MAX_SHAPE_POINTS, 0, 0, 6000, 6000, kind, 450, 0, 0xFFFFFF, band, 32,
                                      out, OPR_GRADIENT_MAX_POINTS, &colour);
            if (m < 0) lost++;
            if (m >= 3) drawn++;
        }
    CHECK(!lost, "a 128-point star loses no band");
    CHECK(drawn >= 60, "and nearly every band has something to draw");
}

/* a shape 400,000 units across: the band corners stay on the shape (no 32-bit overflow, tried with -m32) */
static void test_pdf_far(void)
{
    owf_pdf *pdf = owf_pdf_new();
    unsigned char *data = NULL;
    size_t length = 0;
    int box[8] = {0, 0, 400000, 0, 400000, 1000, 0, 1000};
    CHECK(pdf && owf_pdf_page(pdf, 400000, 4000) == 0, "a wide page");
    CHECK(owf_pdf_gradient(pdf, box, 4, 0, 0, 400000, 1000, OWF_PDF_GRADIENT_LINEAR, 0, OWF_SHADE(0x000000),
                           OWF_SHADE(0xFFFFFF)) == 0, "a linear gradient across it");
    CHECK(owf_pdf_finish(pdf, "g", &data, &length, NULL) == OWF_OK && data, "finished");
    if (data) {
        /* every point of every band within a band's width of the page (points are twentieths: 20,000 wide) */
        /* every "x y m" and "x y l" within the page, points being twentieths (20,000 across) */
        char *text = malloc(length + 1), *tok;
        double prev[2] = {0, 0}, last_start = -1e9;
        int bad = 0, seen = 0, have = 0, starts = 0, order = 1, first_left = 0;
        if (text) {
            memcpy(text, data, length);
            text[length] = 0;
            for (tok = strtok(text, " \n"); tok; tok = strtok(NULL, " \n")) {
                char *e;
                double v = strtod(tok, &e);
                if (e != tok && !*e) {
                    prev[0] = prev[1];
                    prev[1] = v;
                    have = have < 2 ? have + 1 : 2;
                } else {
                    if (have == 2 && (!strcmp(tok, "m") || !strcmp(tok, "l"))) {
                        seen++;
                        if (prev[0] < -1000 || prev[0] > 21000) bad++;
                        if (!strcmp(tok, "m")) {
                            /* each band's path starts at its left edge: after the clip, left to right */
                            if (starts == 1) first_left = prev[0] < 10;
                            if (starts >= 1 && prev[0] + 1 < last_start) order = 0;
                            if (starts >= 1) last_start = prev[0];
                            starts++;
                        }
                    }
                    have = 0;
                }
            }
            free(text);
        }
        CHECK(seen >= 48 * 4, "the bands' corners are there");
        CHECK(!bad, "and all on the page");
        CHECK(starts == 49 && first_left && order, "the bands step left to right from the left edge");
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
    test_screen_bands();
    test_star_bands();
    test_pdf_far();
    test_background();
    printf("gradient: %d checks, %d failed\n", checks, failures);
    return failures != 0;
}
