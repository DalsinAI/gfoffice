#include "openpublisher_core.h"
#include "openpresentation_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks, failed;
#define CHECK(x) do { checks++; if (!(x)) { failed++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)

static int text_has(const owf_story *s, const char *word)
{
    int i, j;
    for (i = 0; i < s->nparas; i++)
        for (j = 0; j < s->paras[i].nruns; j++)
            if (s->paras[i].runs[j].text && strstr(s->paras[i].runs[j].text, word)) return 1;
    return 0;
}

/* The formatting of the first text run that starts with word in a story. */
static const owf_charfmt *run_fmt(const owf_story *s, const char *word)
{
    int i, j;
    for (i = 0; i < s->nparas; i++)
        for (j = 0; j < s->paras[i].nruns; j++)
            if (s->paras[i].runs[j].text && !strncmp(s->paras[i].runs[j].text, word, strlen(word)))
                return &s->paras[i].runs[j].fmt;
    return NULL;
}

/* The run's typeface's name, or "" for the default. */
static const char *font_name(const owf_doc *res, const owf_charfmt *f)
{
    return f && res && f->font >= 0 && f->font < res->nfonts ? res->fonts[f->font].name : "";
}

/* How many objects (all slides) have this fill, line and line width. */
static int count_look(const owf_deck *deck, unsigned long fill, unsigned long line, int width)
{
    int i, j, n = 0;
    for (i = 0; deck && i < deck->nslides; i++)
        for (j = 0; j < deck->slides[i].nobjs; j++) {
            const owf_look *l = &deck->slides[i].objs[j].look;
            if (l->fill == fill && l->line == line && l->line_width == width) n++;
        }
    return n;
}

static int frame_text_ops(const opr_slide *laid, int frame, const char *starts)
{
    size_t i;
    int count = 0, found = 0;
    for (i = 0; i < ol_display_count(laid->doc); ++i) {
        const ol_display_op *op = ol_display_get(laid->doc, i);
        const ol_node *node;
        const char *text;
        if (!op || op->type != OL_OP_TEXT || opr_slide_frame_of(laid, op) != frame) continue;
        count++;
        node = ol_node_by_id(laid->doc, op->u.text.node_id);
        text = node ? ol_node_text(node) : NULL;
        if (text && starts && op->u.text.text_offset == 0 && strstr(text, starts)) found = 1;
    }
    return starts ? found : count;
}

static int frame_for_object(const opr_slide *laid, const owf_obj *object)
{
    int i;
    for (i = 0; i < laid->nframes; ++i)
        if (laid->frames[i].obj == object && !laid->frames[i].from_master) return i;
    return -1;
}

int main(void)
{
    static const char *const corpus[] = {
        "51318.pub", "60685.pub", "Sample.pub", "Sample2000.pub", "Sample98.pub",
        "SampleBrochure.pub", "Sample_2010.pub", "Simple.pub",
        "clusterfuzz-testcase-minimized-POIHPBFFuzzer-4918886059278336.pub",
        "clusterfuzz-testcase-minimized-POIHPBFFuzzer-6325615354773504.pub", "mimetype-pub.pub"
    };
    owf_deck *deck = NULL;
    owf_report *report = owf_report_new();
    unsigned char *pdf = NULL;
    unsigned char *odp = NULL;
    size_t length = 0, odp_length = 0;
    owf_deck *roundtrip = NULL;
    opr_slide *laid = NULL;
    const owf_deck_format *format = owf_deck_format_named("odp"), *used = NULL;
    int version = 0, i, pictures = 0;
    CHECK(report != NULL);
    CHECK(opp_pub_to_deck(NULL, &deck, report) == OWF_ERR_FORMAT && deck == NULL);
    CHECK(opp_import_pub_file("tests/pub/samples/Simple.pub", &deck, report, &version) == OWF_OK);
    CHECK(deck && version == OWF_PUB_2007 && deck->nslides == 1 && deck->nmasters == 1);
    CHECK(deck && deck->width == 11906 && deck->height == 16838 && deck->slides[0].nobjs == 2);
    CHECK(deck && deck->slides[0].objs[0].kind == OWF_OBJ_TEXT && deck->slides[0].objs[0].x == 1134 &&
          deck->slides[0].objs[0].y == 1247 && text_has(&deck->slides[0].objs[0].text, "0123456789abcdef"));
    CHECK(opr_export_pdf(deck, NULL, &pdf, &length, report) == OWF_OK && length > 500 && !memcmp(pdf, "%PDF-", 5));
    CHECK(format && format->export(deck, &odp, &odp_length, report) == OWF_OK && odp_length > 500);
    CHECK(owf_import_deck_memory(odp, odp_length, "odp", &roundtrip, report, &used) == OWF_OK &&
          roundtrip && roundtrip->nslides == 1 && roundtrip->slides[0].nobjs == 2);
    free(odp); owf_deck_free(roundtrip);
    free(pdf); pdf = NULL; owf_deck_free(deck); deck = NULL;

    CHECK(opp_import_pub_file("tests/pub/samples/Sample.pub", &deck, report, &version) == OWF_OK);
    /* P1 character runs (FDPC): Sample's second text box says it is bold, italic, Arial, 20 point;
     * its first is the default (Times New Roman 10 point, from the style, not a run). */
    if (deck && deck->nslides > 0 && deck->slides[0].nobjs > 1) {
        const owf_charfmt *bold = run_fmt(&deck->slides[0].objs[1].text, "This is in bold");
        const owf_charfmt *plain = run_fmt(&deck->slides[0].objs[0].text, "This is some text");
        CHECK(bold && (bold->flags & OWF_BOLD) && (bold->flags & OWF_ITALIC) && bold->size == 400);
        CHECK(!strcmp(font_name(deck->res, bold), "Arial"));
        /* P1b paragraph styles (STSH): the plain text takes the default style's Times New Roman 10 pt */
        CHECK(plain && !(plain->flags & (OWF_BOLD | OWF_ITALIC)) && plain->size == 200);
        CHECK(!strcmp(font_name(deck->res, plain), "Times New Roman"));
        CHECK(format->export(deck, &odp, &odp_length, report) == OWF_OK);
        CHECK(owf_import_deck_memory(odp, odp_length, "odp", &roundtrip, report, &used) == OWF_OK && roundtrip);
        if (roundtrip && roundtrip->nslides > 0 && roundtrip->slides[0].nobjs > 1) {
            const owf_charfmt *again = run_fmt(&roundtrip->slides[0].objs[1].text, "This is in bold");
            CHECK(again && (again->flags & OWF_BOLD) && (again->flags & OWF_ITALIC) && again->size == 400);
            /* ODP saves Arial as Liberation Sans, the deck's own default face, which reads back as the
             * default (-1); the typeface itself is GFOffice's ODP round trip's, not the .pub reader's */
            CHECK(again && (again->font < 0 || !strcmp(font_name(roundtrip->res, again), "Liberation Sans")));
        }
        free(odp); odp = NULL; odp_length = 0;
        owf_deck_free(roundtrip); roundtrip = NULL;
    }
    laid = deck && deck->nslides > 1 ? opr_slide_layout(deck, 1, NULL, NULL, NULL) : NULL;
    CHECK(laid != NULL);
    if (laid) {
        int shapes = 0;
        size_t k;
        for (k = 0; k < ol_display_count(laid->doc); k++) {
            const ol_display_op *op = ol_display_get(laid->doc, k);
            if (op && op->type == OL_OP_SHAPE && op->graphic &&
                op->graphic->shape.type == OL_SHAPE_RECT)
                shapes++;
        }
        CHECK(shapes >= 1);
    }
    opr_slide_free(laid); laid = NULL;
    owf_deck_free(deck); deck = NULL;

    CHECK(opp_import_pub_file("tests/pub/samples/SampleBrochure.pub", &deck, report, &version) == OWF_OK);
    CHECK(deck && deck->nslides == 2 && deck->res->nimages == 8);
    if (deck)
        for (i = 0; i < deck->nslides; i++) {
            int j;
            for (j = 0; j < deck->slides[i].nobjs; j++)
                if (deck->slides[i].objs[j].kind == OWF_OBJ_PICTURE && deck->slides[i].objs[j].image >= 0) pictures++;
        }
    CHECK(pictures >= 3);
    /* "Opening hours" is bold (its run) in its style's Gill Sans MT; the hours are the style's italic
     * Gill Sans MT 11 pt */
    if (deck) {
        int found_bold = 0, found_dash = 0;
        for (i = 0; i < deck->nslides; i++) {
            int j;
            for (j = 0; j < deck->slides[i].nobjs; j++) {
                const owf_charfmt *f = run_fmt(&deck->slides[i].objs[j].text, "Opening hours");
                const owf_charfmt *d = run_fmt(&deck->slides[i].objs[j].text, "Monday");
                if (f && (f->flags & OWF_BOLD) && !strcmp(font_name(deck->res, f), "Gill Sans MT")) found_bold = 1;
                if (d && (d->flags & OWF_ITALIC) && d->size == 220 && !strcmp(font_name(deck->res, d), "Gill Sans MT"))
                    found_dash = 1;
            }
        }
        CHECK(found_bold && found_dash);
    }
    CHECK(opr_export_pdf(deck, NULL, &pdf, &length, report) == OWF_OK && length > 1000 && !memcmp(pdf, "%PDF-", 5));
    free(pdf); owf_deck_free(deck); deck = NULL;

    CHECK(opp_import_pub_file("tests/pub/stress/SampleNewsletter.pub", &deck, report, &version) == OWF_OK);
    if (deck) {
        int linked = 0;
        CHECK(opp_flow_chain_count(deck, &linked) == 10);
        CHECK(linked == 29);
        CHECK(deck->slides[0].objs[0].flow_story == deck->slides[0].objs[1].flow_story);
        CHECK(deck->slides[0].objs[0].flow_story >= 0);
        laid = opr_slide_layout(deck, 0, NULL, NULL, NULL);
        CHECK(laid != NULL);
        if (laid) {
            int first = frame_for_object(laid, &deck->slides[0].objs[0]);
            int second = frame_for_object(laid, &deck->slides[0].objs[1]);
            CHECK(first >= 0 && second >= 0);
            CHECK(frame_text_ops(laid, first, NULL) > 0);
            CHECK(frame_text_ops(laid, second, NULL) > 0);
            CHECK(frame_text_ops(laid, first, "This week in class"));
            CHECK(!frame_text_ops(laid, second, "This week in class"));
        }
        opr_slide_free(laid); laid = NULL;
        CHECK(format->export(deck, &odp, &odp_length, report) == OWF_OK && odp_length > 1000);
        CHECK(owf_import_deck_memory(odp, odp_length, "odp", &roundtrip, report, &used) == OWF_OK && roundtrip);
        if (roundtrip) {
            int linked_again = 0;
            CHECK(opp_flow_chain_count(roundtrip, &linked_again) == 10);
            CHECK(linked_again == 29);
            laid = opr_slide_layout(roundtrip, 0, NULL, NULL, NULL);
            CHECK(laid != NULL);
            if (laid) {
                int first = frame_for_object(laid, &roundtrip->slides[0].objs[0]);
                int second = frame_for_object(laid, &roundtrip->slides[0].objs[1]);
                CHECK(frame_text_ops(laid, first, "This week in class"));
                CHECK(!frame_text_ops(laid, second, "This week in class"));
            }
            opr_slide_free(laid); laid = NULL;
        }
        free(odp); odp = NULL; odp_length = 0;
        owf_deck_free(roundtrip); roundtrip = NULL;
    }
    owf_deck_free(deck); deck = NULL;

    for (i = 0; i < (int)(sizeof corpus / sizeof *corpus); i++) {
        char path[256];
        int rc;
        snprintf(path, sizeof path, "tests/pub/samples/%s", corpus[i]);
        version = -1;
        rc = opp_import_pub_file(path, &deck, report, &version);
        CHECK(rc == OWF_OK || rc == OWF_ERR_FORMAT || rc == OWF_ERR_CORRUPT);
        CHECK((rc == OWF_OK) == (deck != NULL));
        if (deck) {
            CHECK(deck->width > 0 && deck->height > 0 && deck->nslides >= 0 && deck->nmasters > 0);
            owf_deck_free(deck);
            deck = NULL;
        } else {
            CHECK(version == OWF_PUB_UNKNOWN);
        }
    }
    /* P1b: 60685's headings are style 4 (italic Garamond 14 pt, centred), its body style 5 (Garamond 11 pt,
     * centred), as its thumbnail shows them centred */
    CHECK(opp_import_pub_file("tests/pub/samples/60685.pub", &deck, report, &version) == OWF_OK);
    if (deck && deck->nslides > 0 && deck->slides[0].nobjs > 0) {
        const owf_story *st = &deck->slides[0].objs[0].text;
        const owf_charfmt *head = run_fmt(st, "Professional Experience");
        const owf_charfmt *body = run_fmt(st, "Caring Registered Nurse");
        CHECK(head && (head->flags & OWF_ITALIC) && !(head->flags & OWF_BOLD) && head->size == 280);
        CHECK(body && !(body->flags & OWF_ITALIC) && body->size == 220);
        CHECK(!strcmp(font_name(deck->res, head), "Garamond") && !strcmp(font_name(deck->res, body), "Garamond"));
        CHECK(st->nparas > 1 && st->paras[0].fmt.align == OWF_ALIGN_CENTRE && st->paras[1].fmt.align == OWF_ALIGN_CENTRE);
        CHECK(format->export(deck, &odp, &odp_length, report) == OWF_OK);
        CHECK(owf_import_deck_memory(odp, odp_length, "odp", &roundtrip, report, &used) == OWF_OK && roundtrip);
        if (roundtrip && roundtrip->nslides > 0 && roundtrip->slides[0].nobjs > 0) {
            const owf_story *again = &roundtrip->slides[0].objs[0].text;
            const owf_charfmt *h2 = run_fmt(again, "Professional Experience");
            CHECK(again->nparas > 0 && again->paras[0].fmt.align == OWF_ALIGN_CENTRE);
            CHECK(h2 && (h2->flags & OWF_ITALIC) && h2->size == 280);
        }
        free(odp); odp = NULL; odp_length = 0;
        owf_deck_free(roundtrip); roundtrip = NULL;
    }
    owf_deck_free(deck); deck = NULL;
    /* 51318's styles say 0x5A02: the low byte, 2, centres its headline (as its thumbnail shows) */
    CHECK(opp_import_pub_file("tests/pub/samples/51318.pub", &deck, report, &version) == OWF_OK);
    if (deck) {
        int centred = 0, j;
        for (i = 0; i < deck->nslides; i++)
            for (j = 0; j < deck->slides[i].nobjs; j++) {
                const owf_story *st = &deck->slides[i].objs[j].text;
                if (run_fmt(st, "This is where") && st->paras[0].fmt.align == OWF_ALIGN_CENTRE) centred = 1;
            }
        CHECK(centred);
    }
    owf_deck_free(deck); deck = NULL;
    /* P1c frame look ([MS-ODRAW] properties over the colour scheme): 51318's header bar is the scheme's
     * #CCCCEB with a black 1 pt outline, its corner block the scheme's #000099 with none; its border a
     * black 3 pt line; its text boxes' margins 36195 EMU (57 twips). All as its thumbnail shows them. */
    CHECK(opp_import_pub_file("tests/pub/samples/51318.pub", &deck, report, &version) == OWF_OK);
    if (deck) {
        int j, margins = 0;
        CHECK(count_look(deck, OWF_SHADE(0xCCCCEB), OWF_SHADE(0x000000), 20) == 1);
        CHECK(count_look(deck, OWF_SHADE(0x000099), 0, 0) >= 1);
        CHECK(count_look(deck, 0, OWF_SHADE(0x000000), 60) >= 1);
        for (i = 0; i < deck->nslides; i++)
            for (j = 0; j < deck->slides[i].nobjs; j++)
                if (deck->slides[i].objs[j].kind == OWF_OBJ_TEXT && deck->slides[i].objs[j].inset[0] == 57) margins++;
        CHECK(margins >= 2);
        CHECK(format->export(deck, &odp, &odp_length, report) == OWF_OK);
        CHECK(owf_import_deck_memory(odp, odp_length, "odp", &roundtrip, report, &used) == OWF_OK && roundtrip);
        CHECK(count_look(roundtrip, OWF_SHADE(0xCCCCEB), OWF_SHADE(0x000000), 20) == 1);
        free(odp); odp = NULL; odp_length = 0;
        owf_deck_free(roundtrip); roundtrip = NULL;
    }
    owf_deck_free(deck); deck = NULL;
    /* SampleBrochure's scheme starts #000080: its panel border lines are navy, a quarter point */
    CHECK(opp_import_pub_file("tests/pub/samples/SampleBrochure.pub", &deck, report, &version) == OWF_OK);
    CHECK(count_look(deck, 0, OWF_SHADE(0x000080), 5) >= 3);
    CHECK(count_look(deck, OWF_SHADE(0xCCCCE6), 0, 0) >= 2);
    owf_deck_free(deck); deck = NULL;
    /* Simple has no paragraph runs that name a style: every paragraph is the default style's, left */
    CHECK(opp_import_pub_file("tests/pub/samples/Simple.pub", &deck, report, &version) == OWF_OK);
    if (deck && deck->nslides > 0 && deck->slides[0].nobjs > 0) {
        const owf_charfmt *f = run_fmt(&deck->slides[0].objs[0].text, "0123456789");
        CHECK(f && f->size == 200 && !strcmp(font_name(deck->res, f), "Times New Roman"));
        CHECK(deck->slides[0].objs[0].text.paras[0].fmt.align == OWF_ALIGN_LEFT);
    }
    owf_deck_free(deck); deck = NULL;

    owf_report_free(report);
    printf("openpublisher core tests passed (%d checks)\n", checks);
    return failed ? 1 : 0;
}
