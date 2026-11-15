#include "openpublisher_core.h"
#include "openpresentation_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* the .pub files tools/pubgen/make_pub.py makes (make builds them there) */
#ifndef PUB_SAMPLES
#define PUB_SAMPLES "build/host/pubgen"
#endif
#define PUB(name) PUB_SAMPLES "/" name

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

/* The link of the first run in the deck that starts with word; NULL: none, or no link. */
static const char *deck_href(const owf_deck *deck, const char *word)
{
    int i, j, k, m;
    for (i = 0; deck && i < deck->nslides; i++)
        for (j = 0; j < deck->slides[i].nobjs; j++) {
            const owf_story *st = &deck->slides[i].objs[j].text;
            for (k = 0; k < st->nparas; k++)
                for (m = 0; m < st->paras[k].nruns; m++) {
                    const owf_run *run = &st->paras[k].runs[m];
                    if (run->text && !strncmp(run->text, word, strlen(word)))
                        return run->href;
                }
        }
    return NULL;
}

/* Every object of the deck whose story has a run starting with word: its paragraph's alignment, or -1. */
static int align_of(const owf_deck *deck, const char *word)
{
    int i, j, k;
    for (i = 0; deck && i < deck->nslides; i++)
        for (j = 0; j < deck->slides[i].nobjs; j++) {
            const owf_story *st = &deck->slides[i].objs[j].text;
            for (k = 0; k < st->nparas; k++)
                if (st->paras[k].nruns && st->paras[k].runs[0].text &&
                    !strncmp(st->paras[k].runs[0].text, word, strlen(word)))
                    return (int)st->paras[k].fmt.align;
        }
    return -1;
}

/* The first run starting with word anywhere in the deck. */
static const owf_charfmt *deck_run(const owf_deck *deck, const char *word)
{
    int i, j;
    for (i = 0; deck && i < deck->nslides; i++)
        for (j = 0; j < deck->slides[i].nobjs; j++) {
            const owf_charfmt *f = run_fmt(&deck->slides[i].objs[j].text, word);
            if (f) return f;
        }
    return NULL;
}

/* A story's text, all runs, into buf. */
static void story_text(const owf_story *s, char *buf, size_t size)
{
    int i, j;
    size_t n = 0;
    buf[0] = 0;
    for (i = 0; i < s->nparas; i++)
        for (j = 0; j < s->paras[i].nruns; j++)
            if (s->paras[i].runs[j].text) {
                size_t k = strlen(s->paras[i].runs[j].text);
                if (n + k + 1 >= size) return;
                memcpy(buf + n, s->paras[i].runs[j].text, k + 1);
                n += k;
            }
}

static int same_fmt(const owf_charfmt *a, const owf_charfmt *b)
{
    return a->flags == b->flags && a->size == b->size && a->font == b->font && a->colour == b->colour;
}

/* Two objects alike as a .pub keeps them: kind, box, group, picture, look, text. */
static int same_obj(const owf_obj *a, const owf_obj *b, const owf_deck *da, const owf_deck *db)
{
    static char ta[4096], tb[4096];
    if (a->kind != b->kind || a->x != b->x || a->y != b->y || a->width != b->width || a->height != b->height ||
        a->parent != b->parent || a->look.fill != b->look.fill || a->look.line != b->look.line ||
        (a->look.line && a->look.line_width != b->look.line_width) || (a->flow_story >= 0) != (b->flow_story >= 0))
        return 0;
    if (a->kind == OWF_OBJ_PICTURE) {
        const owf_image *ia, *ib;
        if ((a->image < 0) != (b->image < 0)) return 0;
        if (a->image >= 0) {
            ia = &da->res->images[a->image];
            ib = &db->res->images[b->image];
            if (ia->length != ib->length || memcmp(ia->data, ib->data, ia->length) || strcmp(ia->mime, ib->mime))
                return 0;
        }
    }
    if (a->kind == OWF_OBJ_TABLE && a->ncols != b->ncols) return 0;
    story_text(&a->text, ta, sizeof ta);
    story_text(&b->text, tb, sizeof tb);
    if (strcmp(ta, tb) || a->text.nparas != b->text.nparas) return 0;
    if (a->text.nparas && a->text.paras[0].nruns && b->text.paras[0].nruns &&
        !same_fmt(&a->text.paras[0].runs[0].fmt, &b->text.paras[0].runs[0].fmt))
        return 0;
    return 1;
}

/* A publication's deck saved as .pub (opp_export_pub) and read back gives the
 * same deck; saving that again gives the same bytes. */
static void deck_round_trip(const char *path, owf_report *report)
{
    owf_deck *a = NULL, *b = NULL;
    owf_pub *pub = NULL;
    unsigned char *one = NULL, *two = NULL;
    size_t n1 = 0, n2 = 0;
    int i, j, same = 1, objects = 0;
    CHECK(opp_import_pub_file(path, &a, report, NULL) == OWF_OK && a);
    if (!a) return;
    CHECK(opp_export_pub(a, &one, &n1, report) == OWF_OK && one && n1 > 512);
    CHECK(one && owf_pub_read(one, n1, &pub, report) == OWF_OK && pub);
    CHECK(pub && opp_pub_to_deck(pub, &b, report) == OWF_OK && b);
    if (b) {
        CHECK(b->nslides == a->nslides && b->nmasters == a->nmasters);
        for (i = 0; i < a->nslides && i < b->nslides; i++) {
            if (a->slides[i].nobjs != b->slides[i].nobjs || a->slides[i].master != b->slides[i].master) {
                same = 0;
                fprintf(stderr, "%s: page %d: %d objects, master %d; read back %d, %d\n", path, i + 1,
                        a->slides[i].nobjs, a->slides[i].master, b->slides[i].nobjs, b->slides[i].master);
                continue;
            }
            for (j = 0; j < a->slides[i].nobjs; j++, objects++)
                if (!same_obj(&a->slides[i].objs[j], &b->slides[i].objs[j], a, b)) {
                    same = 0;
                    fprintf(stderr, "%s: page %d object %d differs when read back\n", path, i + 1, j);
                }
        }
        CHECK(same);
        CHECK(opp_flow_chain_count(a, NULL) == opp_flow_chain_count(b, NULL));
        CHECK(opp_export_pub(b, &two, &n2, report) == OWF_OK && two);
        CHECK(two && n1 == n2 && !memcmp(one, two, n1));
    }
    CHECK(objects > 0);
    owf_free(one);
    owf_free(two);
    owf_pub_free(pub);
    owf_deck_free(a);
    owf_deck_free(b);
}

int main(void)
{
    static const char *const corpus[] = {
        PUB("gen-2007-plain.pub"), PUB("gen-2007-runs.pub"), PUB("gen-2010-styles.pub"), PUB("gen-2013-look.pub"),
        PUB("gen-2003-columns.pub"), PUB("gen-2003-names.pub"), PUB("gen-98.pub"), PUB("gen-2000.pub"),
        PUB("gen-cut.pub"), PUB("gen-notpub.pub"), PUB("gen-damaged.pub")
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

    /* the plainest file: one text box at 20 x 22 mm on an A4 page, its margins 91440 EMU (144 twips) */
    CHECK(opp_import_pub_file(PUB("gen-2007-plain.pub"), &deck, report, &version) == OWF_OK);
    CHECK(deck && version == OWF_PUB_2007 && deck->nslides == 1 && deck->nmasters == 1);
    CHECK(deck && deck->width == 11906 && deck->height == 16838 && deck->slides[0].nobjs == 1);
    CHECK(deck && deck->slides[0].objs[0].kind == OWF_OBJ_TEXT && deck->slides[0].objs[0].x == 1134 &&
          deck->slides[0].objs[0].y == 1247 && text_has(&deck->slides[0].objs[0].text, "0123456789"));
    CHECK(deck && deck->slides[0].objs[0].inset[0] == 144 && deck->slides[0].objs[0].inset[1] == 72);
    CHECK(opr_export_pdf(deck, NULL, &pdf, &length, report) == OWF_OK && length > 500 && !memcmp(pdf, "%PDF-", 5));
    CHECK(format && format->export(deck, &odp, &odp_length, report) == OWF_OK && odp_length > 500);
    CHECK(owf_import_deck_memory(odp, odp_length, "odp", &roundtrip, report, &used) == OWF_OK &&
          roundtrip && roundtrip->nslides == 1 && roundtrip->slides[0].nobjs == 1);
    free(odp); odp = NULL; owf_deck_free(roundtrip); roundtrip = NULL;
    free(pdf); pdf = NULL; owf_deck_free(deck); deck = NULL;

    /* P1 character runs (FDPC): bold, italic, Arial, 20 point; Courier New 12; bold alone over the default;
     * the plain text takes the default style's Times New Roman 10 pt. A table, a PNG, a second page and a
     * frame on the scratch area (left out). */
    CHECK(opp_import_pub_file(PUB("gen-2007-runs.pub"), &deck, report, &version) == OWF_OK);
    CHECK(deck && deck->nslides == 2 && deck->slides[0].nobjs == 4 && deck->res->nimages == 1);
    /* P1d links (TOKN): a web address and an e-mail on their words; a link to a page is text for now;
     * the file carries a second TOKN for the story (linking "See"), which is ignored */
    CHECK(deck_href(deck, "our site") && !strcmp(deck_href(deck, "our site"), "https://example.org/gfoffice/"));
    CHECK(deck_href(deck, "write to us") && !strcmp(deck_href(deck, "write to us"), "mailto:team@example.org?subject=Hello"));
    CHECK(deck_href(deck, "See ") == NULL && deck_href(deck, ", or turn to page 1.") == NULL);
    /* P1b underline (0x21E's low byte): on the two linked words only */
    CHECK(deck_run(deck, "our site") && (deck_run(deck, "our site")->flags & OWF_UNDERLINE));
    CHECK(deck_run(deck, "write to us") && (deck_run(deck, "write to us")->flags & OWF_UNDERLINE));
    CHECK(deck_run(deck, "See ") && !(deck_run(deck, "See ")->flags & OWF_UNDERLINE));
    CHECK(deck_run(deck, "Bold and italic") && !(deck_run(deck, "Bold and italic")->flags & OWF_UNDERLINE));
    if (deck && deck->nslides > 0 && deck->slides[0].nobjs > 3) {
        const owf_charfmt *bold = run_fmt(&deck->slides[0].objs[1].text, "Bold and italic");
        const owf_charfmt *plain = run_fmt(&deck->slides[0].objs[0].text, "Plain text");
        const owf_charfmt *courier = run_fmt(&deck->slides[0].objs[1].text, "Then Courier");
        const owf_charfmt *alone = run_fmt(&deck->slides[0].objs[1].text, "bold alone");
        CHECK(bold && (bold->flags & OWF_BOLD) && (bold->flags & OWF_ITALIC) && bold->size == 400);
        CHECK(!strcmp(font_name(deck->res, bold), "Arial"));
        CHECK(courier && !(courier->flags & (OWF_BOLD | OWF_ITALIC)) && courier->size == 240 &&
              !strcmp(font_name(deck->res, courier), "Courier New"));
        CHECK(alone && (alone->flags & OWF_BOLD) && alone->size == 200 && !strcmp(font_name(deck->res, alone), "Times New Roman"));
        CHECK(plain && !(plain->flags & (OWF_BOLD | OWF_ITALIC)) && plain->size == 200);
        CHECK(!strcmp(font_name(deck->res, plain), "Times New Roman"));
        CHECK(deck->slides[0].objs[2].kind == OWF_OBJ_TABLE && deck->slides[0].objs[2].ncols == 2);
        /* P1d table cells (TCD): six cells in three rows of two; the third cell has two paragraphs */
        {
            const owf_story *t = &deck->slides[0].objs[2].text;
            CHECK(t->nparas == 7);
            if (t->nparas == 7) {
                CHECK(t->paras[0].table_id >= 0 && t->paras[0].table_row == 0 && t->paras[0].table_col == 0 &&
                      t->paras[0].table_cols == 2);
                CHECK(t->paras[1].table_row == 0 && t->paras[1].table_col == 1);
                CHECK(t->paras[2].table_row == 1 && t->paras[2].table_col == 0);
                CHECK(t->paras[3].table_row == 1 && t->paras[3].table_col == 0);
                CHECK(t->paras[4].table_row == 1 && t->paras[4].table_col == 1);
                CHECK(t->paras[6].table_row == 2 && t->paras[6].table_col == 1);
            }
            /* the plain text boxes' paragraphs are not cells */
            CHECK(deck->slides[0].objs[0].text.nparas && deck->slides[0].objs[0].text.paras[0].table_id < 0);
        }
        CHECK(deck->slides[0].objs[3].kind == OWF_OBJ_PICTURE && deck->slides[0].objs[3].image == 0);
        CHECK(format->export(deck, &odp, &odp_length, report) == OWF_OK);
        CHECK(owf_import_deck_memory(odp, odp_length, "odp", &roundtrip, report, &used) == OWF_OK && roundtrip);
        if (roundtrip && roundtrip->nslides > 0 && roundtrip->slides[0].nobjs > 1) {
            const owf_charfmt *again = run_fmt(&roundtrip->slides[0].objs[1].text, "Bold and italic");
            CHECK(again && (again->flags & OWF_BOLD) && (again->flags & OWF_ITALIC) && again->size == 400);
            /* ODP saves Arial as Liberation Sans, the deck's own default face, which reads back as the
             * default (-1); the typeface itself is GFOffice's ODP round trip's, not the .pub reader's */
            CHECK(again && (again->font < 0 || !strcmp(font_name(roundtrip->res, again), "Liberation Sans")));
            CHECK(deck_href(roundtrip, "our site") && !strcmp(deck_href(roundtrip, "our site"), "https://example.org/gfoffice/"));
            CHECK(deck_run(roundtrip, "our site") && (deck_run(roundtrip, "our site")->flags & OWF_UNDERLINE));
            if (roundtrip->slides[0].nobjs > 2) {
                const owf_story *t = &roundtrip->slides[0].objs[2].text;
                int k, last_row = -1, cells_ok = t->nparas >= 6;
                for (k = 0; k < t->nparas; k++)
                    if (t->paras[k].table_id < 0 || t->paras[k].table_row < last_row) cells_ok = 0;
                    else last_row = t->paras[k].table_row;
                CHECK(roundtrip->slides[0].objs[2].kind == OWF_OBJ_TABLE && cells_ok && last_row == 2);
            }
        }
        free(odp); odp = NULL; odp_length = 0;
        owf_deck_free(roundtrip); roundtrip = NULL;
    }
    laid = deck && deck->nslides > 1 ? opr_slide_layout(deck, 0, NULL, NULL, NULL) : NULL;
    CHECK(laid != NULL);
    if (laid) {
        int first = frame_for_object(laid, &deck->slides[0].objs[0]);
        CHECK(first >= 0 && frame_text_ops(laid, first, "Plain text"));
    }
    opr_slide_free(laid); laid = NULL;
    CHECK(opr_export_pdf(deck, NULL, &pdf, &length, report) == OWF_OK && length > 1000 && !memcmp(pdf, "%PDF-", 5));
    free(pdf); pdf = NULL; owf_deck_free(deck); deck = NULL;

    /* P1b paragraph styles (STSH) and alignment (0x204's low byte): style 1 italic Garamond 14 pt centred,
     * style 2 Garamond 11 pt, style 3 bold Arial right; a paragraph's own alignment, left too, over its
     * style's; 6 (not known) keeps the style's; a style not in the sheet is the default */
    CHECK(opp_import_pub_file(PUB("gen-2010-styles.pub"), &deck, report, &version) == OWF_OK);
    CHECK(deck && version == OWF_PUB_2010);
    if (deck) {
        const owf_charfmt *head = deck_run(deck, "A centred heading");
        const owf_charfmt *body = deck_run(deck, "Body text");
        const owf_charfmt *right = deck_run(deck, "Page 2");
        const owf_charfmt *none = deck_run(deck, "A style not in the sheet");
        CHECK(head && (head->flags & OWF_ITALIC) && !(head->flags & OWF_BOLD) && head->size == 280);
        CHECK(body && !(body->flags & OWF_ITALIC) && body->size == 220);
        CHECK(!strcmp(font_name(deck->res, head), "Garamond") && !strcmp(font_name(deck->res, body), "Garamond"));
        CHECK(right && (right->flags & OWF_BOLD) && right->size == 200 && !strcmp(font_name(deck->res, right), "Arial"));
        CHECK(none && none->size == 200 && !strcmp(font_name(deck->res, none), "Times New Roman"));
        CHECK(align_of(deck, "A centred heading") == OWF_ALIGN_CENTRE);
        CHECK(align_of(deck, "Body text") == OWF_ALIGN_LEFT);
        CHECK(align_of(deck, "A body paragraph centred") == OWF_ALIGN_CENTRE);
        CHECK(align_of(deck, "Page 2") == OWF_ALIGN_RIGHT);
        CHECK(align_of(deck, "Left over a right style") == OWF_ALIGN_LEFT);
        CHECK(align_of(deck, "Left, with a high byte") == OWF_ALIGN_LEFT);
        CHECK(align_of(deck, "An alignment not known") == OWF_ALIGN_LEFT);
        CHECK(format->export(deck, &odp, &odp_length, report) == OWF_OK);
        CHECK(owf_import_deck_memory(odp, odp_length, "odp", &roundtrip, report, &used) == OWF_OK && roundtrip);
        if (roundtrip) {
            const owf_charfmt *h2 = deck_run(roundtrip, "A centred heading");
            CHECK(align_of(roundtrip, "A centred heading") == OWF_ALIGN_CENTRE);
            CHECK(align_of(roundtrip, "Page 2") == OWF_ALIGN_RIGHT);
            CHECK(h2 && (h2->flags & OWF_ITALIC) && h2->size == 280);
        }
        free(odp); odp = NULL; odp_length = 0;
        owf_deck_free(roundtrip); roundtrip = NULL;
    }
    owf_deck_free(deck); deck = NULL;

    /* P1c frame look ([MS-ODRAW] properties over the colour scheme): the headline bar is the scheme's
     * #CCCCEB with a black 1 pt outline, the corner block the scheme's #000099 with none, the border a
     * black 3 pt line; in the group, a back filled with no colour said is white, the front #996633 with
     * the scheme's #993300 half a point; colours not known (a system colour, a scheme place past its
     * end) are left as not said */
    CHECK(opp_import_pub_file(PUB("gen-2013-look.pub"), &deck, report, &version) == OWF_OK);
    CHECK(deck && version == OWF_PUB_2013);
    if (deck) {
        CHECK(count_look(deck, OWF_SHADE(0xCCCCEB), OWF_SHADE(0x000000), 20) == 1);
        CHECK(count_look(deck, OWF_SHADE(0x000099), 0, 0) == 1);
        CHECK(count_look(deck, 0, OWF_SHADE(0x000000), 60) == 1);
        CHECK(count_look(deck, OWF_SHADE(0xFFFFFF), 0, 0) == 1);
        CHECK(count_look(deck, OWF_SHADE(0x996633), OWF_SHADE(0x993300), 10) == 1);
        CHECK(deck->slides[0].objs[0].inset[0] == 144);
        /* P1b text colour (0x244, a place in PL): red as 0x00BBGGRR, the scheme's place 2, and one not known */
        CHECK(deck_run(deck, "headline") && deck_run(deck, "headline")->colour == 0xFF0000);
        CHECK(deck_run(deck, " over") && deck_run(deck, " over")->colour == 0x000099);
        CHECK(deck_run(deck, " a bar") && deck_run(deck, " a bar")->colour == OWF_COLOUR_AUTO);
        CHECK(deck_run(deck, "A ") && deck_run(deck, "A ")->colour == OWF_COLOUR_AUTO);
        CHECK(format->export(deck, &odp, &odp_length, report) == OWF_OK);
        CHECK(owf_import_deck_memory(odp, odp_length, "odp", &roundtrip, report, &used) == OWF_OK && roundtrip);
        CHECK(count_look(roundtrip, OWF_SHADE(0xCCCCEB), OWF_SHADE(0x000000), 20) == 1);
        CHECK(deck_run(roundtrip, "headline") && deck_run(roundtrip, "headline")->colour == 0xFF0000);
        free(odp); odp = NULL; odp_length = 0;
        owf_deck_free(roundtrip); roundtrip = NULL;
    }
    owf_deck_free(deck); deck = NULL;

    /* Linked frames: a story through three columns that the file lists middle, left, right, and one that
     * runs on to page two. The chain (frame field 0x28) puts the columns left to right, so the layout
     * starts the story in the left one; four pictures (GIF, JPEG, WMF, DIB) and WordArt */
    CHECK(opp_import_pub_file(PUB("gen-2003-columns.pub"), &deck, report, &version) == OWF_OK);
    CHECK(deck && version == OWF_PUB_2003 && deck->nslides == 2 && deck->res->nimages == 4);
    if (deck) {
        int linked = 0, j;
        for (i = 0; i < deck->nslides; i++)
            for (j = 0; j < deck->slides[i].nobjs; j++)
                if (deck->slides[i].objs[j].kind == OWF_OBJ_PICTURE && deck->slides[i].objs[j].image >= 0) pictures++;
        CHECK(pictures == 4);
        CHECK(opp_flow_chain_count(deck, &linked) == 2);
        CHECK(linked == 5);
        CHECK(deck->slides[0].nobjs >= 3 && deck->slides[0].objs[0].flow_story >= 0 &&
              deck->slides[0].objs[0].flow_story == deck->slides[0].objs[1].flow_story &&
              deck->slides[0].objs[1].flow_story == deck->slides[0].objs[2].flow_story);
        CHECK(deck->slides[0].objs[0].x < deck->slides[0].objs[1].x && deck->slides[0].objs[1].x < deck->slides[0].objs[2].x);
        CHECK(text_has(&deck->slides[0].objs[4].text, "Our WordArt"));
        laid = opr_slide_layout(deck, 0, NULL, NULL, NULL);
        CHECK(laid != NULL);
        if (laid) {
            int first = frame_for_object(laid, &deck->slides[0].objs[0]);
            int second = frame_for_object(laid, &deck->slides[0].objs[1]);
            CHECK(first >= 0 && second >= 0);
            CHECK(frame_text_ops(laid, first, NULL) > 0);
            CHECK(frame_text_ops(laid, second, NULL) > 0);
            CHECK(frame_text_ops(laid, first, "Column story, part one"));
            CHECK(!frame_text_ops(laid, second, "Column story, part one"));
        }
        opr_slide_free(laid); laid = NULL;
        CHECK(format->export(deck, &odp, &odp_length, report) == OWF_OK && odp_length > 1000);
        CHECK(owf_import_deck_memory(odp, odp_length, "odp", &roundtrip, report, &used) == OWF_OK && roundtrip);
        if (roundtrip) {
            int linked_again = 0;
            CHECK(opp_flow_chain_count(roundtrip, &linked_again) == 2);
            CHECK(linked_again == 5);
            laid = opr_slide_layout(roundtrip, 0, NULL, NULL, NULL);
            CHECK(laid != NULL);
            if (laid) {
                int first = frame_for_object(laid, &roundtrip->slides[0].objs[0]);
                int second = frame_for_object(laid, &roundtrip->slides[0].objs[1]);
                CHECK(frame_text_ops(laid, first, "Column story, part one"));
                CHECK(!frame_text_ops(laid, second, "Column story, part one"));
            }
            opr_slide_free(laid); laid = NULL;
        }
        free(odp); odp = NULL; odp_length = 0;
        owf_deck_free(roundtrip); roundtrip = NULL;
        CHECK(opr_export_pdf(deck, NULL, &pdf, &length, report) == OWF_OK && length > 1000 && !memcmp(pdf, "%PDF-", 5));
        free(pdf); pdf = NULL;
    }
    owf_deck_free(deck); deck = NULL;

    /* a named master; a tab, a line break, a character past U+FFFF; a frame whose text isn't there */
    CHECK(opp_import_pub_file(PUB("gen-2003-names.pub"), &deck, report, &version) == OWF_OK);
    CHECK(deck && deck->nslides == 1 && deck->nmasters == 1 && deck->slides[0].nobjs == 3);
    if (deck && deck->nslides == 1 && deck->slides[0].nobjs == 3) {
        CHECK(text_has(&deck->slides[0].objs[0].text, "\xf0\x9d\x84\x9e"));      /* U+1D11E */
        CHECK(!deck->slides[0].objs[1].text.nparas);
    }
    owf_deck_free(deck); deck = NULL;

    /* the 98/2000 form: a text frame with its runs (plain fields for the typeface), a table, a shape */
    for (i = 0; i < 2; i++) {
        CHECK(opp_import_pub_file(i ? PUB("gen-2000.pub") : PUB("gen-98.pub"), &deck, report, &version) == OWF_OK);
        CHECK(deck && version == (i ? OWF_PUB_2000 : OWF_PUB_98) && deck->nslides == 1 && deck->slides[0].nobjs == 3);
        if (deck && deck->nslides == 1 && deck->slides[0].nobjs == 3) {
            const owf_charfmt *b = run_fmt(&deck->slides[0].objs[0].text, "bold Arial");
            CHECK(b && (b->flags & OWF_BOLD) && b->size == 240 && !strcmp(font_name(deck->res, b), "Arial"));
            CHECK(deck->slides[0].objs[1].kind == OWF_OBJ_TABLE && deck->slides[0].objs[1].ncols == 2);
        }
        owf_deck_free(deck); deck = NULL;
    }

    /* damaged files: one cut short still opens; one not a publication; one with a Contents that isn't */
    CHECK(opp_import_pub_file(PUB("gen-cut.pub"), &deck, report, &version) == OWF_OK && deck);
    owf_deck_free(deck); deck = NULL;
    CHECK(opp_import_pub_file(PUB("gen-notpub.pub"), &deck, report, &version) == OWF_ERR_FORMAT && !deck);
    CHECK(opp_import_pub_file(PUB("gen-damaged.pub"), &deck, report, &version) == OWF_ERR_CORRUPT && !deck);

    for (i = 0; i < (int)(sizeof corpus / sizeof *corpus); i++) {
        int rc;
        version = -1;
        rc = opp_import_pub_file(corpus[i], &deck, report, &version);
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

    /* GFOpenPublisher save: the deck back to .pub */
    deck_round_trip(PUB("gen-2007-plain.pub"), report);
    deck_round_trip(PUB("gen-2007-runs.pub"), report);
    deck_round_trip(PUB("gen-2010-styles.pub"), report);
    deck_round_trip(PUB("gen-2013-look.pub"), report);
    deck_round_trip(PUB("gen-2003-columns.pub"), report);
    deck_round_trip(PUB("gen-2003-names.pub"), report);
    {
        owf_pub *none = NULL;
        unsigned char *data = (unsigned char *)"x";
        size_t n = 1;
        owf_deck *empty = owf_deck_new_empty();
        CHECK(opp_deck_to_pub(NULL, &none, report) == OWF_ERR_FORMAT && !none);
        CHECK(opp_export_pub(NULL, &data, &n, report) == OWF_ERR_FORMAT && !data && !n);
        CHECK(opp_export_pub_file(empty, NULL, report) == OWF_ERR_FORMAT);
        if (empty) {
            empty->width = 11906;
            empty->height = 16838;
            CHECK(opp_export_pub(empty, &data, &n, report) == OWF_OK && data && n > 512);
            owf_free(data);
            owf_deck_free(empty);
        }
    }

    owf_report_free(report);
    printf("openpublisher core tests passed (%d checks)\n", checks);
    return failed ? 1 : 0;
}
