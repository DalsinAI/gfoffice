/*
 * Hatched and see-through fills and shadows on slides (Presentation gap 1): ODP's draw:hatch,
 * draw:opacity and draw:shadow and PPTX's a:pattFill, a:alpha and a:outerShdw read into owf_look and
 * the slide backgrounds, saved back to both, and kept when slides are copied to another deck. The
 * files are written here. make fills-test, make fills-check (ASan).
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

static const char FODP[] =
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
    "<draw:hatch draw:name=\"lines\" draw:style=\"single\" draw:color=\"#ff0000\" draw:distance=\"0.1cm\" "
    "draw:rotation=\"450\"/>"
    "<draw:hatch draw:name=\"grid\" draw:style=\"double\" draw:color=\"#0000ff\" draw:distance=\"0.2cm\" "
    "draw:rotation=\"0\"/>"
    "<draw:hatch draw:name=\"three\" draw:style=\"triple\" draw:color=\"#008000\" draw:rotation=\"30deg\"/>"
    "<draw:opacity draw:name=\"fade\" draw:style=\"linear\" draw:start=\"0%\" draw:end=\"100%\"/>"
    "</office:styles>"
    "<office:automatic-styles>"
    "<style:style style:name=\"gr1\" style:family=\"graphic\"><style:graphic-properties draw:fill=\"hatch\" "
    "draw:fill-hatch-name=\"lines\" draw:fill-hatch-solid=\"true\" draw:fill-color=\"#ffff00\" draw:opacity=\"60%\" "
    "draw:shadow=\"visible\" draw:shadow-offset-x=\"0.2cm\" draw:shadow-offset-y=\"0.1cm\" "
    "draw:shadow-color=\"#333333\" draw:shadow-opacity=\"50%\" draw:stroke=\"none\"/></style:style>"
    "<style:style style:name=\"gr2\" style:family=\"graphic\"><style:graphic-properties draw:fill=\"hatch\" "
    "draw:fill-hatch-name=\"grid\" draw:fill-hatch-solid=\"false\" draw:stroke=\"none\"/></style:style>"
    "<style:style style:name=\"gr3\" style:family=\"graphic\"><style:graphic-properties draw:fill=\"solid\" "
    "draw:fill-color=\"#00ff00\" draw:opacity-name=\"fade\" draw:stroke=\"none\"/></style:style>"
    "<style:style style:name=\"gr4\" style:family=\"graphic\"><style:graphic-properties draw:fill=\"solid\" "
    "draw:fill-color=\"#112233\" draw:shadow=\"visible\" draw:shadow-blur=\"0.1cm\" draw:stroke=\"none\"/>"
    "</style:style>"
    "<style:style style:name=\"gr5\" style:family=\"graphic\"><style:graphic-properties draw:fill=\"hatch\" "
    "draw:fill-hatch-name=\"missing\" draw:fill-color=\"#654321\" draw:stroke=\"none\"/></style:style>"
    "<style:style style:name=\"gr6\" style:family=\"graphic\"><style:graphic-properties draw:fill=\"hatch\" "
    "draw:fill-hatch-name=\"three\" draw:fill-hatch-solid=\"true\" draw:fill-color=\"#ffffff\" "
    "draw:stroke=\"none\"/></style:style>"
    "<style:style style:name=\"dp1\" style:family=\"drawing-page\"><style:drawing-page-properties "
    "draw:fill=\"hatch\" draw:fill-hatch-name=\"grid\" draw:fill-hatch-solid=\"true\" draw:fill-color=\"#ccccff\" "
    "draw:opacity=\"80%\"/></style:style>"
    "</office:automatic-styles>"
    "<office:body><office:presentation><draw:page draw:name=\"one\" draw:style-name=\"dp1\">"
    "<draw:rect draw:style-name=\"gr1\" svg:x=\"1cm\" svg:y=\"1cm\" svg:width=\"4cm\" svg:height=\"2cm\"/>"
    "<draw:rect draw:style-name=\"gr2\" svg:x=\"6cm\" svg:y=\"1cm\" svg:width=\"4cm\" svg:height=\"2cm\"/>"
    "<draw:ellipse draw:style-name=\"gr3\" svg:x=\"1cm\" svg:y=\"4cm\" svg:width=\"4cm\" svg:height=\"4cm\"/>"
    "<draw:rect draw:style-name=\"gr4\" svg:x=\"6cm\" svg:y=\"4cm\" svg:width=\"4cm\" svg:height=\"2cm\"/>"
    "<draw:rect draw:style-name=\"gr5\" svg:x=\"11cm\" svg:y=\"1cm\" svg:width=\"4cm\" svg:height=\"2cm\"/>"
    "<draw:rect draw:style-name=\"gr6\" svg:x=\"11cm\" svg:y=\"4cm\" svg:width=\"4cm\" svg:height=\"2cm\"/>"
    "</draw:page></office:presentation></office:body></office:document>";

static const owf_obj *nth_shape(const owf_deck *d, int n)
{
    int i;
    if (!d || !d->nslides) return NULL;
    for (i = 0; i < d->slides[0].nobjs; i++)
        if (d->slides[0].objs[i].kind == OWF_OBJ_SHAPE && !n--) return &d->slides[0].objs[i];
    return NULL;
}

static int near(int a, int b, int by)
{
    return a - b <= by && b - a <= by;
}

static int noted(owf_report *report, const char *what)
{
    int k;
    for (k = 0; report && k < owf_report_count(report); k++)
        if (strstr(owf_report_note(report, k, NULL, NULL), what)) return 1;
    return 0;
}

/* exact: the file is ODP (or our ODP read back), so distances and the kept triple hatch are exact; PPTX
 * has preset patterns only (no distance, a triple drawn double) */
static void check_all(const owf_deck *d, const char *where, int exact)
{
    const owf_obj *a = nth_shape(d, 0), *b = nth_shape(d, 1), *c = nth_shape(d, 2), *e = nth_shape(d, 3);
    const owf_obj *f = nth_shape(d, 4), *g = nth_shape(d, 5);
    char what[200];
    snprintf(what, sizeof what, "%s: six shapes", where);
    CHECK(a && b && c && e && f && g, what);
    if (!a || !b || !c || !e || !f || !g) return;

    snprintf(what, sizeof what, "%s: red lines at 45 degrees over yellow, 40%% see-through", where);
    CHECK(a->look.hatch == OWF_HATCH_SINGLE && a->look.hatch_angle == 450 &&
          a->look.hatch_colour == OWF_SHADE(0xFF0000) && a->look.fill == OWF_SHADE(0xFFFF00) &&
          a->look.transparency == 40, what);
    if (exact) {
        snprintf(what, sizeof what, "%s: the lines 1 mm apart (56 twips, 0.1cm truncated)", where);
        CHECK(a->look.hatch_distance == 56, what);
    }
    snprintf(what, sizeof what, "%s: its shadow 2 mm right, 1 mm down, dark grey, half see-through", where);
    CHECK(a->look.shadow && near(a->look.shadow_dx, 113, exact ? 0 : 2) && near(a->look.shadow_dy, 56, exact ? 0 : 2) &&
          a->look.shadow_colour == OWF_SHADE(0x333333) && a->look.shadow_transparency == 50, what);

    snprintf(what, sizeof what, "%s: a blue grid on its own (no fill)", where);
    CHECK(b->look.hatch == OWF_HATCH_DOUBLE && b->look.hatch_angle == 0 &&
          b->look.hatch_colour == OWF_SHADE(0x0000FF) && !OWF_SHADE_SET(b->look.fill) && OWF_LOOK_FILLED(&b->look) &&
          b->look.transparency == 0 && !b->look.shadow, what);

    snprintf(what, sizeof what, "%s: green, its graded transparency drawn evenly at 50%%", where);
    CHECK(c->look.hatch == OWF_HATCH_NONE && c->look.fill == OWF_SHADE(0x00FF00) && c->look.transparency == 50, what);

    snprintf(what, sizeof what, "%s: a shadow with no offset given: 2 mm each way, grey, solid", where);
    CHECK(e->look.shadow && near(e->look.shadow_dx, 113, exact ? 0 : 2) && near(e->look.shadow_dy, 113, exact ? 0 : 2) &&
          e->look.shadow_colour == OWF_SHADE(0x808080) && e->look.shadow_transparency == 0, what);

    snprintf(what, sizeof what, "%s: a hatch not there: its fill colour, plain", where);
    CHECK(f->look.hatch == OWF_HATCH_NONE && f->look.fill == OWF_SHADE(0x654321), what);

    snprintf(what, sizeof what, "%s: green triple hatching at 30 degrees over white", where);
    CHECK(g->look.hatch == (exact ? OWF_HATCH_TRIPLE : OWF_HATCH_DOUBLE) && g->look.fill == OWF_SHADE(0xFFFFFF) &&
          g->look.hatch_colour == OWF_SHADE(0x008000) && (!exact || g->look.hatch_angle == 300), what);

    snprintf(what, sizeof what, "%s: the background a blue grid over pale blue, 20%% see-through", where);
    CHECK(d->slides[0].background_hatch == OWF_HATCH_DOUBLE && d->slides[0].background_hatch_angle == 0 &&
          d->slides[0].background_hatch_colour == OWF_SHADE(0x0000FF) &&
          d->slides[0].background == OWF_SHADE(0xCCCCFF) && d->slides[0].background_transparency == 20, what);
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
    CHECK(noted(report, "Graded transparency is drawn even all over"), "the graded transparency reported");
    CHECK(noted(report, "Soft shadows are drawn with a sharp edge"), "the soft shadow reported");
    CHECK(noted(report, "Picture fills are drawn in one colour"), "the missing hatch reported");
    CHECK(odp && odp->export, "ODP saves");
    if (deck && odp && odp->export) {
        owf_report *quiet = owf_report_new();
        CHECK(odp->export(deck, &data, &length, NULL) == OWF_OK, "saved as ODP");
        CHECK(owf_import_deck_memory(data, length, "odp", &again, quiet, NULL) == OWF_OK, "the ODP read back");
        check_all(again, "ODP saved and read", 1);
        /* what was saved is plain: one opacity per fill, sharp shadows, every hatch there */
        CHECK(!noted(quiet, "Graded transparency") && !noted(quiet, "Soft shadows") && !noted(quiet, "one colour"),
              "nothing approximated reading our own file");
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

/* a slide copied into another deck (the slide clipboard) keeps its hatched, see-through background, and
 * its objects their looks */
static void test_clip(const owf_deck *deck)
{
    opr_clip *clip = opr_clip_new();
    opr_undo *u = opr_undo_new();
    owf_deck *dst = NULL;
    int one = 0, sel = -1;
    /* the deck to paste into: the same file read again, so it has a master and layouts */
    CHECK(owf_import_deck_memory((const unsigned char *)FODP, sizeof FODP - 1, "fodp", &dst, NULL, NULL) == OWF_OK,
          "a second deck");
    CHECK(clip && u && dst && opr_clip_copy(clip, deck, &one, 1) == 0, "the slide copied");
    if (clip && u && dst && opr_clip_paste(clip, u, dst, 0, &sel) == 0 && dst->nslides == 2) {
        const owf_slide *s = &dst->slides[1];
        CHECK(s->background_hatch == OWF_HATCH_DOUBLE && s->background_hatch_colour == OWF_SHADE(0x0000FF) &&
              s->background == OWF_SHADE(0xCCCCFF) && s->background_transparency == 20,
              "pasted: the background's hatching and transparency kept");
        CHECK(s->nobjs >= 1 && s->objs[0].look.shadow && s->objs[0].look.transparency == 40,
              "pasted: the first shape's shadow and transparency kept");
    } else {
        CHECK(0, "pasted after the first slide");
    }
    opr_undo_free(u);
    opr_clip_free(clip);
    owf_deck_free(dst);
}

int main(void)
{
    owf_deck *deck = NULL;
    test_odp(&deck);
    if (deck) {
        test_pptx(deck);
        test_clip(deck);
    }
    owf_deck_free(deck);
    printf("fills: %d checks, %d failed\n", checks, failures);
    return failures != 0;
}
