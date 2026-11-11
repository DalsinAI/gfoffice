/*
 * test_opr_notes: speaker notes, notes pages and handouts
 * (src/deck/opr_notes.c, opr_layout.c's notes page, opr_pdf.c):
 *
 *   - the notes page's size (A4 when the deck has none) and its slide and
 *     text boxes, inside the margins, the slide's shape kept;
 *   - handout boxes for 1, 2, 3, 4, 6 and 9 slides a page: inside the
 *     margins, apart, in rows;
 *   - notes typed in through the text calls (OPR_NOTES), set and read as
 *     one string, each change one Undo step with Redo; notes on a copied
 *     slide are its own;
 *   - the notes page laid out: the text frame is OPR_NOTES, the caret and
 *     a hit find it;
 *   - notes saved as ODP and read back;
 *   - PDFs of notes pages and handouts: a page a slide, and per_page
 *     slides a page.
 *
 * Prints what fails; exit 1 when anything did.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "openpresentation_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures, checks;

#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
    printf(__VA_ARGS__); printf("\n"); } } while (0)

static owf_deck *deck_of(int n, opr_undo *u)
{
    owf_deck *d = owf_deck_new(OWF_DECK_16_9_WIDTH, OWF_DECK_16_9_HEIGHT);
    int i, sel = 0;
    char words[32];
    if (!d)
        return NULL;
    for (i = 0; i < n; i++) {
        if (opr_slide_insert(u, d, i, d->nlayouts > 1 ? 1 : -1, &sel) < 0)
            break;
        snprintf(words, sizeof words, "Title %c", 'A' + i);
        opr_text_set(d, i, 0, words);
    }
    opr_undo_clear(u);
    return d;
}

static const char *notes(const owf_deck *d, int slide)
{
    static char buf[512];
    char *t = opr_notes_get(d, slide);
    snprintf(buf, sizeof buf, "%s", t ? t : "(null)");
    owf_free(t);
    return buf;
}

static int inside(const int b[4], int x, int y, int w, int h)
{
    return b[2] > 0 && b[3] > 0 && b[0] >= x && b[1] >= y && b[0] + b[2] <= x + w && b[1] + b[3] <= y + h;
}

static int apart(const int a[4], const int b[4])
{
    return a[0] + a[2] <= b[0] || b[0] + b[2] <= a[0] || a[1] + a[3] <= b[1] || b[1] + b[3] <= a[1];
}

static void pages(void)
{
    opr_undo *u = opr_undo_new();
    owf_deck *d = deck_of(1, u);
    int w, h, s[4], t[4], b[9][4], per[6] = { 1, 2, 3, 4, 6, 9 }, i, k;
    long ratio_deck, ratio_box;

    opr_notes_page(d, &w, &h);
    CHECK(w == 11906 && h == 16838, "the notes page is A4 portrait: %d x %d", w, h);
    d->notes_page.width = d->notes_page.height = 0;
    opr_notes_page(d, &w, &h);
    CHECK(w == 11906 && h == 16838, "no size: A4 (%d x %d)", w, h);
    opr_notes_boxes(d, s, t);
    CHECK(inside(s, 1134, 1134, w - 2268, h - 2268) && inside(t, 1134, 1134, w - 2268, h - 2268),
          "both boxes inside the margins");
    CHECK(s[1] == 1134 && t[1] >= s[1] + s[3], "the slide at the top, the notes under it");
    CHECK(s[3] <= (h - 2268) / 2, "the slide no taller than half the page");
    ratio_deck = (long)d->width * 1000 / d->height;
    ratio_box = (long)s[2] * 1000 / s[3];
    CHECK(ratio_box - ratio_deck < 3 && ratio_deck - ratio_box < 3, "the slide's shape kept: %ld %ld",
          ratio_deck, ratio_box);
    CHECK(t[2] == w - 2268 && t[1] + t[3] == h - 1134, "the notes fill the rest");

    for (i = 0; i < 6; i++) {
        int ok = 1, rows = 1;
        for (k = 0; k < per[i]; k++) {
            if (opr_handout_box(d, per[i], k, b[k]) < 0 || !inside(b[k], 1134, 1134, w - 2268, h - 2268))
                ok = 0;
            if (k && b[k][1] > b[k - 1][1])
                rows++;
        }
        for (k = 1; k < per[i]; k++)
            if (!apart(b[k - 1], b[k]))
                ok = 0;
        CHECK(ok, "%d a page: each inside the margins and apart", per[i]);
        CHECK(rows == (per[i] == 4 ? 2 : per[i] == 6 ? 3 : per[i] == 9 ? 3 : per[i]),
              "%d a page: %d rows", per[i], rows);
        CHECK(opr_handout_box(d, per[i], per[i], b[0]) < 0 && opr_handout_box(d, per[i], -1, b[0]) < 0,
              "%d a page: no box past the page", per[i]);
    }
    CHECK(opr_handout_box(d, 5, 5, b[0]) == 0 && opr_handout_box(d, 5, 6, b[0]) < 0, "5: six a page");
    owf_deck_free(d);
    opr_undo_free(u);
}

static void typing(void)
{
    opr_undo *u = opr_undo_new();
    owf_deck *d = deck_of(3, u);
    opr_pos at;
    int slide = -1, obj = -1, sel = 1;

    CHECK(!strcmp(notes(d, 1), ""), "no notes yet: \"%s\"", notes(d, 1));
    CHECK(opr_obj_editable(d, 1, OPR_NOTES), "the notes can be typed in");
    CHECK(!opr_notes_set(u, d, 1, "First point\nSecond point"), "notes set");
    CHECK(!strcmp(notes(d, 1), "First point\nSecond point") && d->slides[1].notes.text.nparas == 2,
          "two paragraphs: \"%s\"", notes(d, 1));
    CHECK(!strcmp(notes(d, 0), "") && !strcmp(notes(d, 2), ""), "only that slide's");
    CHECK(opr_undo_push(u, d, 1, OPR_NOTES) == 0, "a step before typing");
    at.para = 1;
    at.offset = 6;
    CHECK(opr_text_insert(d, 1, OPR_NOTES, &at, " big", 4) == 0, "typed into the notes");
    CHECK(!strcmp(notes(d, 1), "First point\nSecond big point"), "typed: \"%s\"", notes(d, 1));
    CHECK(!opr_undo_undo(u, d, &slide, &obj) && slide == 1 && obj == OPR_NOTES &&
          !strcmp(notes(d, 1), "First point\nSecond point"), "Undo the typing: \"%s\" (%d %d)", notes(d, 1),
          slide, obj);
    CHECK(!opr_undo_undo(u, d, NULL, NULL) && !strcmp(notes(d, 1), ""), "Undo the set");
    CHECK(!opr_undo_redo(u, d, NULL, NULL) && !strcmp(notes(d, 1), "First point\nSecond point"), "Redo");

    CHECK(opr_slide_duplicate(u, d, 1, 2, &sel) >= 0 && d->nslides == 4 &&
          !strcmp(notes(d, 2), "First point\nSecond point"), "a copied slide has the notes");
    CHECK(!opr_notes_set(u, d, 2, "Changed"), "the copy's notes changed");
    CHECK(!strcmp(notes(d, 1), "First point\nSecond point") && !strcmp(notes(d, 2), "Changed"),
          "the copy's notes are its own");
    CHECK(opr_notes_set(u, d, 9, "x") < 0 && opr_notes_set(u, d, -1, "x") < 0, "no such slide");
    owf_deck_free(d);
    opr_undo_free(u);
}

static void layout(void)
{
    opr_undo *u = opr_undo_new();
    owf_deck *d = deck_of(2, u);
    opr_slide *s;
    int frame, t[4], w, h;
    opr_pos at;

    opr_notes_set(u, d, 0, "Speak slowly\nThen the demo");
    s = opr_notes_layout(d, 0, NULL, NULL);
    CHECK(s && s->notes && s->slide == 0 && s->nframes == 2, "a notes page: %d frames", s ? s->nframes : -1);
    if (s) {
        frame = opr_slide_frame_for(s, OPR_NOTES);
        CHECK(frame == 1 && opr_frame_obj(s, frame) == OPR_NOTES && opr_frame_obj(s, 0) < 0,
              "frame 1 is the notes (%d)", frame);
        CHECK(opr_slide_frame_for(s, 0) < 0, "no slide object on the notes page");
        opr_notes_boxes(d, NULL, t);
        CHECK(s->frames[1].box.x == t[0] && s->frames[1].box.y == t[1] && s->frames[1].box.width == t[2],
              "the notes in their box");
        opr_notes_page(d, &w, &h);
        CHECK(ol_page_count(s->doc) == 1, "one page");
        {
            ol_rect b;
            at.para = 1;
            at.offset = 0;
            CHECK(opr_caret_box(s, frame, at, NULL, NULL, &b) && b.y >= (ol_unit)t[1] * OL_UNITS_PER_TWIP &&
                  b.x >= (ol_unit)t[0] * OL_UNITS_PER_TWIP, "the caret in the notes: %ld,%ld", (long)b.x, (long)b.y);
        }
        opr_slide_free(s);
    }
    s = opr_notes_layout(d, 1, NULL, NULL);
    CHECK(s && opr_slide_frame_for(s, OPR_NOTES) == 1, "empty notes are still a frame");
    opr_slide_free(s);
    s = opr_slide_layout(d, 0, NULL, NULL, "1/1/2026");
    CHECK(s && !s->notes && opr_slide_frame_for(s, OPR_NOTES) < 0, "a slide's own layout has no notes frame");
    opr_slide_free(s);
    CHECK(!opr_notes_layout(d, 5, NULL, NULL), "no such slide");
    owf_deck_free(d);
    opr_undo_free(u);
}

static int count(const unsigned char *data, size_t n, const char *word)
{
    size_t k = strlen(word), i;
    int c = 0;
    for (i = 0; i + k <= n; i++)
        if (!memcmp(data + i, word, k))
            c++;
    return c;
}

static void saving(void)
{
    opr_undo *u = opr_undo_new();
    owf_deck *d = deck_of(7, u), *back = NULL;
    const owf_deck_format *odp = owf_deck_format_named("odp");
    opr_pdf_options o;
    unsigned char *data = NULL;
    size_t n = 0;

    opr_notes_set(u, d, 0, "Welcome everyone");
    opr_notes_set(u, d, 3, "Numbers\nfrom the survey");
    CHECK(odp && odp->export && odp->export(d, &data, &n, NULL) == OWF_OK, "saved as ODP");
    CHECK(data && owf_import_deck_memory(data, n, "odp", &back, NULL, NULL) == OWF_OK && back && back->nslides == 7 && !strcmp(notes(back, 0), "Welcome everyone") &&
          !strcmp(notes(back, 3), "Numbers\nfrom the survey") && !strcmp(notes(back, 1), ""),
          "the notes read back: \"%s\" \"%s\"", back ? notes(back, 0) : "", back ? notes(back, 3) : "");
    owf_deck_free(back);
    owf_free(data);
    data = NULL;

    memset(&o, 0, sizeof o);
    o.today = "1/1/2026";
    CHECK(opr_export_pdf(d, &o, &data, &n, NULL) == OWF_OK && data, "the slides as a PDF");
    {
        int slides = data ? count(data, n, "/MediaBox") : 0;
        owf_free(data);
        data = NULL;
        o.pages = OPR_PDF_NOTES;
        CHECK(opr_export_pdf(d, &o, &data, &n, NULL) == OWF_OK && data, "the notes pages as a PDF");
        CHECK(data && count(data, n, "/MediaBox") == slides && slides == 7, "a notes page a slide: %d %d",
              data ? count(data, n, "/MediaBox") : -1, slides);
        CHECK(data && count(data, n, "[0 0 595.") + count(data, n, "[0 0 595 ") >= 1,
              "A4 pages for the notes");
        owf_free(data);
        data = NULL;
        o.pages = OPR_PDF_HANDOUTS;
        o.handout_slides = 3;
        CHECK(opr_export_pdf(d, &o, &data, &n, NULL) == OWF_OK && data && count(data, n, "/MediaBox") == 3,
              "seven slides three a page: three pages (%d)", data ? count(data, n, "/MediaBox") : -1);
        owf_free(data);
        data = NULL;
        o.handout_slides = 9;
        CHECK(opr_export_pdf(d, &o, &data, &n, NULL) == OWF_OK && data && count(data, n, "/MediaBox") == 1,
              "nine a page: one page");
        owf_free(data);
        data = NULL;
        d->slides[1].hidden = 1;
        o.pages = OPR_PDF_NOTES;
        CHECK(opr_export_pdf(d, &o, &data, &n, NULL) == OWF_OK && data && count(data, n, "/MediaBox") == 6,
              "a hidden slide left out of the notes");
        owf_free(data);
    }
    owf_deck_free(d);
    opr_undo_free(u);
}

int main(void)
{
    pages();
    typing();
    layout();
    saving();
    printf("test_opr_notes: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
