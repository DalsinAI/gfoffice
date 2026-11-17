/*
 * Picture fills on text boxes and rectangles, and tiled picture backgrounds (Presentation gap 1, part 2):
 * read from ODP (draw:fill="bitmap", style:repeat, draw:fill-image-width/-height) and saved back, saved
 * to PPTX (a:blipFill with a:stretch or a:tile) and read back, kept on the slide clipboard, and laid out
 * (the picture stretched, or tiles cropped at the box's edge, under the text). make picture-fills-test,
 * make picture-fills-check (ASan).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "openpresentation_core.h"

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

/* a 96 by 48 pixel red PNG, no resolution given: 96 dots an inch, an inch by half an inch */
#define PNG_B64                                                                                            \
    "iVBORw0KGgoAAAANSUhEUgAAAGAAAAAwCAIAAABhdOiYAAAAVklEQVR42u3QQQkAAAgEsItz/VMYywb+hcESLNNyiAJBggQJEiRIkC" \
    "AECRIkSJAgQYIQJEiQIEGCBAlCkCBBggQJEiRIEIIECRIkSJAgQQgSJEiQoB8WOc2xHq+UzqIAAAAASUVORK5CYII="
#define PNG_BYTES 143

static const char FODP[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
    "<office:document xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" "
    "xmlns:style=\"urn:oasis:names:tc:opendocument:xmlns:style:1.0\" "
    "xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\" "
    "xmlns:draw=\"urn:oasis:names:tc:opendocument:xmlns:drawing:1.0\" "
    "xmlns:svg=\"urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0\" "
    "xmlns:presentation=\"urn:oasis:names:tc:opendocument:xmlns:presentation:1.0\" "
    "office:version=\"1.2\" office:mimetype=\"application/vnd.oasis.opendocument.presentation\">"
    "<office:styles>"
    "<draw:fill-image draw:name=\"Red\"><office:binary-data>" PNG_B64 "</office:binary-data></draw:fill-image>"
    "</office:styles>"
    "<office:automatic-styles>"
    /* stretched over a dark blue fill colour */
    "<style:style style:name=\"gr1\" style:family=\"graphic\"><style:graphic-properties draw:fill=\"bitmap\" "
    "draw:fill-image-name=\"Red\" style:repeat=\"stretch\" draw:fill-color=\"#112233\" draw:stroke=\"none\"/>"
    "</style:style>"
    /* tiles 1 cm by 5 mm, 40% see-through */
    "<style:style style:name=\"gr2\" style:family=\"graphic\"><style:graphic-properties draw:fill=\"bitmap\" "
    "draw:fill-image-name=\"Red\" style:repeat=\"repeat\" draw:fill-image-width=\"1cm\" "
    "draw:fill-image-height=\"0.5cm\" draw:opacity=\"60%\" draw:stroke=\"none\"/></style:style>"
    /* tiles at the picture's own size */
    "<style:style style:name=\"gr3\" style:family=\"graphic\"><style:graphic-properties draw:fill=\"bitmap\" "
    "draw:fill-image-name=\"Red\" style:repeat=\"repeat\" draw:fill-image-width=\"100%\" "
    "draw:fill-image-height=\"100%\" draw:stroke=\"none\"/></style:style>"
    /* an ellipse's picture fill: its colour */
    "<style:style style:name=\"gr4\" style:family=\"graphic\"><style:graphic-properties draw:fill=\"bitmap\" "
    "draw:fill-image-name=\"Red\" draw:fill-color=\"#445566\" draw:stroke=\"none\"/></style:style>"
    "<style:style style:name=\"dp1\" style:family=\"drawing-page\"><style:drawing-page-properties "
    "draw:fill=\"bitmap\" draw:fill-image-name=\"Red\" style:repeat=\"repeat\"/></style:style>"
    "</office:automatic-styles>"
    "<office:body><office:presentation><draw:page draw:name=\"one\" draw:style-name=\"dp1\">"
    "<draw:rect draw:style-name=\"gr1\" svg:x=\"1cm\" svg:y=\"1cm\" svg:width=\"4cm\" svg:height=\"2cm\"/>"
    "<draw:frame draw:style-name=\"gr2\" svg:x=\"6cm\" svg:y=\"1cm\" svg:width=\"5.3cm\" svg:height=\"1.8cm\">"
    "<draw:text-box><text:p>Tiled</text:p></draw:text-box></draw:frame>"
    "<draw:rect draw:style-name=\"gr3\" svg:x=\"1cm\" svg:y=\"5cm\" svg:width=\"6cm\" svg:height=\"3cm\"/>"
    "<draw:ellipse draw:style-name=\"gr4\" svg:x=\"8cm\" svg:y=\"5cm\" svg:width=\"3cm\" svg:height=\"3cm\"/>"
    "</draw:page></office:presentation></office:body></office:document>";

static size_t count(const unsigned char *data, size_t length, const char *what)
{
    size_t n = 0, k, w = strlen(what);
    for (k = 0; k + w <= length; k++)
        if (!memcmp(data + k, what, w)) n++;
    return n;
}

static int noted(owf_report *report, const char *what)
{
    int k;
    for (k = 0; report && k < owf_report_count(report); k++)
        if (strstr(owf_report_note(report, k, NULL, NULL), what)) return 1;
    return 0;
}

static int near(int a, int b, int tol)
{
    return a - b <= tol && b - a <= tol;
}

static int is_png(const owf_deck *d, int image)
{
    return image >= 0 && image < d->res->nimages && d->res->images[image].length == PNG_BYTES;
}

/* what every reading has; exact: the ODP's (the fill colour under a picture is ODP's only) */
static void check_all(const owf_deck *d, const char *where, int exact)
{
    const owf_obj *o;
    char what[160];
    int tol = exact ? 0 : 2;
    CHECK(d && d->nslides == 1 && d->slides[0].nobjs == 4, where);
    if (!d || d->nslides != 1 || d->slides[0].nobjs != 4)
        return;
    o = d->slides[0].objs;
    snprintf(what, sizeof what, "%s: a stretched picture on the rectangle", where);
    CHECK(o[0].kind == OWF_OBJ_SHAPE && o[0].look.picture == OWF_PICTURE_STRETCH && is_png(d, o[0].image), what);
    if (exact) {
        snprintf(what, sizeof what, "%s: over its fill colour", where);
        CHECK(o[0].look.fill == OWF_SHADE(0x112233), what);
    }
    snprintf(what, sizeof what, "%s: the text box tiled 1 cm by 5 mm, 40%% see-through", where);
    CHECK(o[1].kind == OWF_OBJ_TEXT && o[1].look.picture == OWF_PICTURE_TILE && is_png(d, o[1].image) &&
          near(o[1].look.tile_width, 566, tol) && near(o[1].look.tile_height, 283, tol) &&
          o[1].look.transparency == 40 && o[1].text.nparas == 1, what);
    snprintf(what, sizeof what, "%s: the second rectangle tiled at the picture's own size", where);
    CHECK(o[2].look.picture == OWF_PICTURE_TILE && is_png(d, o[2].image) && !o[2].look.tile_width &&
          !o[2].look.tile_height, what);
    snprintf(what, sizeof what, "%s: the ellipse in its colour", where);
    CHECK(o[3].shape == OWF_SHAPE_ELLIPSE && o[3].look.picture == OWF_PICTURE_NONE && o[3].image < 0 &&
          o[3].look.fill == OWF_SHADE(0x445566), what);
    snprintf(what, sizeof what, "%s: the background tiled at the picture's own size", where);
    CHECK(is_png(d, d->slides[0].background_image) && d->slides[0].background_tile == 1 &&
          !d->slides[0].background_tile_width && !d->slides[0].background_tile_height, what);
}

static void test_odp(owf_deck **out)
{
    owf_deck *deck = NULL, *again = NULL;
    owf_report *report = owf_report_new();
    const owf_deck_format *odp = owf_deck_format_named("odp");
    unsigned char *data = NULL;
    size_t length = 0;
    CHECK(owf_import_deck_memory((const unsigned char *)FODP, sizeof FODP - 1, "fodp", &deck, report, NULL) == OWF_OK,
          "the flat ODP read");
    check_all(deck, "ODP", 1);
    CHECK(noted(report, "Picture fills are drawn in one colour"), "the ellipse's picture fill reported");
    CHECK(!noted(report, "sized in percent"), "100% tiles are the picture's own size, not reported");
    if (deck) {
        int w = 0, h = 0;
        CHECK(owf_deck_image_size(&deck->res->images[deck->slides[0].objs[0].image], &w, &h) && w == 1440 &&
              h == 720, "the picture's own size: an inch by half an inch");
    }
    if (deck && odp && odp->export) {
        owf_report *quiet = owf_report_new();
        CHECK(odp->export(deck, &data, &length, NULL) == OWF_OK, "saved as ODP");
        CHECK(owf_import_deck_memory(data, length, "odp", &again, quiet, NULL) == OWF_OK, "the ODP read back");
        check_all(again, "ODP saved and read", 1);
        CHECK(!noted(quiet, "one colour") && !noted(quiet, "drawn stretched"), "nothing approximated reading our own file");
        owf_report_free(quiet);
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
    CHECK(owf_import_deck_memory(data, length, "pptx", &again, NULL, NULL) == OWF_OK, "the PPTX read back");
    check_all(again, "PPTX saved and read", 0);
    free(data);
    owf_deck_free(again);
}

static void test_clip(const owf_deck *deck)
{
    opr_clip *clip = opr_clip_new();
    opr_undo *u = opr_undo_new();
    owf_deck *dst = NULL;
    int one = 0, sel = -1;
    CHECK(owf_import_deck_memory((const unsigned char *)FODP, sizeof FODP - 1, "fodp", &dst, NULL, NULL) == OWF_OK,
          "a second deck");
    CHECK(clip && u && dst && opr_clip_copy(clip, deck, &one, 1) == 0, "the slide copied");
    if (clip && u && dst && opr_clip_paste(clip, u, dst, 0, &sel) == 0 && dst->nslides == 2) {
        const owf_slide *s = &dst->slides[1];
        CHECK(s->nobjs == 4 && s->objs[0].look.picture == OWF_PICTURE_STRETCH && is_png(dst, s->objs[0].image) &&
              s->objs[1].look.picture == OWF_PICTURE_TILE && is_png(dst, s->objs[1].image),
              "pasted: the picture fills and their pictures kept");
        CHECK(is_png(dst, s->background_image) && s->background_tile == 1, "pasted: the tiled background kept");
    } else {
        CHECK(0, "pasted after the first slide");
    }
    opr_undo_free(u);
    opr_clip_free(clip);
    owf_deck_free(dst);
}

/* the tiles a box w by h is drawn with, tiled tw by th, as opr_layout.c lays them */
static int tiles(int w, int h, int tw, int th)
{
    while (w / tw >= 32 || h / th >= 32) {
        tw *= 2;
        th *= 2;
    }
    return ((w + tw - 1) / tw) * ((h + th - 1) / th);
}

static int frame_of_obj(const opr_slide *laid, const owf_obj *o)
{
    int f;
    for (f = 0; f < laid->nframes; f++)
        if (laid->frames[f].obj == o)
            return f;
    return -1;
}

/* a picture fill's frame: just before its object's, showing no object */
static int fill_frame_of(const opr_slide *laid, const owf_obj *o)
{
    int f = frame_of_obj(laid, o);
    return f > 0 && !laid->frames[f - 1].obj ? f - 1 : -1;
}

/* the display op index of the frame's first op of the type, or -1 */
static int first_op(const opr_slide *laid, int frame, int type)
{
    size_t i;
    for (i = 0; i < ol_display_count(laid->doc); i++) {
        const ol_display_op *op = ol_display_get(laid->doc, i);
        if ((int)op->type == type && opr_slide_frame_of(laid, op) == frame)
            return (int)i;
    }
    return -1;
}

/* the frame's image ops; *cropped: how many have a right or bottom crop; *last: the index of the last */
static int images_in(const opr_slide *laid, int frame, int *cropped, int *last)
{
    size_t i;
    int n = 0;
    *cropped = 0;
    *last = -1;
    for (i = 0; frame >= 0 && i < ol_display_count(laid->doc); i++) {
        const ol_display_op *op = ol_display_get(laid->doc, i);
        if (op->type != OL_OP_IMAGE || opr_slide_frame_of(laid, op) != frame)
            continue;
        n++;
        *last = (int)i;
        if (op->graphic && (op->graphic->crop.right || op->graphic->crop.bottom))
            ++*cropped;
    }
    return n;
}

static void test_layout_and_pdf(const owf_deck *deck)
{
    opr_slide *laid = opr_slide_layout(deck, 0, NULL, NULL, "1 January 2026");
    unsigned char *pdf = NULL;
    size_t length = 0;
    int expect = 0;
    CHECK(laid != NULL, "laid out");
    if (laid) {
        const owf_obj *o = deck->slides[0].objs;
        int cropped, last, n;
        n = images_in(laid, 0, &cropped, &last);
        CHECK(n == tiles(deck->width, deck->height, 1440, 720) && n > 1, "the background in tiles of its own size");
        expect += n;
        n = images_in(laid, fill_frame_of(laid, &o[0]), &cropped, &last);
        CHECK(n == 1 && !cropped, "the rectangle: one picture stretched over it");
        CHECK(!images_in(laid, frame_of_obj(laid, &o[0]), &cropped, &last), "none in the rectangle's own frame");
        expect += n;
        n = images_in(laid, fill_frame_of(laid, &o[1]), &cropped, &last);
        CHECK(n == tiles(o[1].width, o[1].height, o[1].look.tile_width, o[1].look.tile_height) && n > 4 &&
              cropped > 0, "the text box: tiles 1 cm by 5 mm, those at its edges cropped");
        CHECK(first_op(laid, frame_of_obj(laid, &o[1]), OL_OP_TEXT) > last, "the text box: its text over the tiles");
        expect += n;
        n = images_in(laid, fill_frame_of(laid, &o[2]), &cropped, &last);
        CHECK(n == tiles(o[2].width, o[2].height, 1440, 720), "the second rectangle: tiles of the picture's size");
        expect += n;
        n = images_in(laid, frame_of_obj(laid, &o[3]), &cropped, &last);
        CHECK(n == 0 && fill_frame_of(laid, &o[3]) < 0, "the ellipse: no picture");
        opr_slide_free(laid);
    }
    CHECK(opr_export_pdf(deck, NULL, &pdf, &length, NULL) == OWF_OK && pdf, "the PDF");
    if (pdf)
        CHECK((int)count(pdf, length, " Do") >= expect, "every picture and tile drawn in the PDF");
    free(pdf);
}

int main(void)
{
    owf_deck *deck = NULL;
    test_odp(&deck);
    if (deck) {
        test_pptx(deck);
        test_clip(deck);
        test_layout_and_pdf(deck);
    }
    owf_deck_free(deck);
    printf("picture-fills: %d checks, %d failed\n", checks, failures);
    return failures != 0;
}
