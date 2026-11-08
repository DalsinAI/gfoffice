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
    owf_report_free(report);
    printf("openpublisher core tests passed (%d checks)\n", checks);
    return failed ? 1 : 0;
}
