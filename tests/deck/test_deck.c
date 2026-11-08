/*
 * test_deck: OpenPresentation's model, ODP and the page PDF writer, from C;
 * with OPR_WITH_LAYOUT (OpenLayout there), its slide layout and PDF too.
 *
 *   - a new deck: its master, layouts and placeholders; slides added,
 *     moved and deleted; groups deleted whole; list levels; what an
 *     object takes from its layout and master;
 *   - a deck built here, saved as ODP and flat ODP and read back the same;
 *   - the PDF writer's file: every object where the cross-reference table
 *     says, and text widths from the standard fonts;
 *   - slides laid out: frames at their boxes, a centred title in the
 *     middle of its box, bullets before their text, text that shrinks to
 *     fit, and the deck's PDF a page a slide, hidden ones left out.
 *
 * Prints what fails; exit 1 when anything did.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "owf_deck.h"
#include "owf_pdf.h"
#ifdef OPR_WITH_LAYOUT
#include "openpresentation_core.h"
#include "ow_layout.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures, checks;

#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
    printf(__VA_ARGS__); printf("\n"); } } while (0)

static owf_deck *current;

static void add_text(owf_obj *o, const char *text, int level, unsigned flags)
{
    owf_para *p = owf_deck_add_para(current, current ? &current->masters[0] : NULL, o, level);
    owf_charfmt f;
    owf_charfmt_init(&f);
    f.flags = flags;
    if (p)
        owf_para_add_text(p, &f, text, strlen(text));
}

static owf_deck *build_deck(void)
{
    owf_deck *d = owf_deck_new(OWF_DECK_16_9_WIDTH, OWF_DECK_16_9_HEIGHT);
    owf_slide *s;
    owf_obj *o;
    static const unsigned char png[] = {
        0x89, 'P', 'N', 'G', 13, 10, 26, 10, 0, 0, 0, 13, 'I', 'H', 'D', 'R', 0, 0, 0, 1, 0, 0, 0, 1, 8, 2, 0, 0, 0,
        0x90, 0x77, 0x53, 0xDE, 0, 0, 0, 12, 'I', 'D', 'A', 'T', 0x08, 0xD7, 0x63, 0xF8, 0xCF, 0xC0, 0x00, 0x00,
        0x03, 0x01, 0x01, 0x00, 0x18, 0xDD, 0x8D, 0xB0, 0, 0, 0, 0, 'I', 'E', 'N', 'D', 0xAE, 0x42, 0x60, 0x82
    };
    if (!d)
        return NULL;
    current = d;
    owf_doc_set_title(d->res, "Built in C");
    /* 1: title slide */
    s = owf_deck_add_slide(d, -1, 1);
    if (!s)
        return d;
    s->objs[0].flags &= ~(unsigned)OWF_OBJ_EMPTY;
    add_text(&s->objs[0], "A deck from C", 0, 0);
    s->objs[1].flags &= ~(unsigned)OWF_OBJ_EMPTY;
    add_text(&s->objs[1], "subtitle", 0, OWF_ITALIC);
    s->transition.kind = OWF_TRANS_PUSH;
    s->transition.direction = OWF_DIR_UP;
    s->transition.duration = 500;
    s->transition.advance_after = 3000;
    owf_deck_set_text(&s->footer, "Footer text");
    s->show = OWF_SHOW_FOOTER | OWF_SHOW_NUMBER;
    /* 2: title and content, with levels, a shape and a picture */
    s = owf_deck_add_slide(d, -1, 2);
    if (!s)
        return d;
    s->objs[0].flags &= ~(unsigned)OWF_OBJ_EMPTY;
    add_text(&s->objs[0], "Points", 0, 0);
    s->objs[1].flags &= ~(unsigned)OWF_OBJ_EMPTY;
    add_text(&s->objs[1], "first", 1, 0);
    add_text(&s->objs[1], "second, deeper", 2, OWF_BOLD);
    add_text(&s->objs[1], "third", 1, 0);
    o = owf_slide_add(s, OWF_OBJ_SHAPE);
    if (o) {
        o->shape = OWF_SHAPE_ELLIPSE;
        o->x = 12000; o->y = 6000; o->width = 3000; o->height = 2000;
        o->look.fill = OWF_SHADE(0x336699);
        o->look.line = OWF_SHADE(0x000000);
        o->look.line_width = 20;
        o->valign = OWF_VALIGN_CENTRE;
        add_text(o, "oval", 0, 0);
    }
    o = owf_slide_add(s, OWF_OBJ_PICTURE);
    if (o) {
        o->x = 1000; o->y = 7000; o->width = 1440; o->height = 1440;
        o->image = owf_doc_add_image(d->res, "dot.png", "image/png", png, sizeof png, 1440, 1440, "a dot");
    }
    owf_story_add(&s->notes, NULL);
    owf_para_add_text(&s->notes.paras[0], &s->objs[0].text.paras[0].runs[0].fmt, "notes!", 6);
    /* 3: hidden */
    s = owf_deck_add_slide(d, -1, 0);
    if (s)
        s->hidden = 1;
    return d;
}

static void test_model(void)
{
    owf_deck *d = owf_deck_new(0, 0);
    owf_slide *s;
    owf_obj box, *o;
    const owf_textlevel *levels;
    int n, i;

    CHECK(d != NULL, "owf_deck_new");
    if (!d)
        return;
    current = d;
    CHECK(d->nmasters == 1 && d->nlayouts == 6, "a master and six layouts: %d %d", d->nmasters, d->nlayouts);
    CHECK(d->width == 15874 && d->height == 8929, "Impress's 16:9 by default: %d x %d", d->width, d->height);
    CHECK(d->masters[0].nobjs == 5, "five master placeholders: %d", d->masters[0].nobjs);
    s = owf_deck_add_slide(d, -1, 2);
    CHECK(s && s->nobjs == 2, "Title, Content gives two placeholders");
    if (!s) {
        owf_deck_free(d);
        return;
    }
    CHECK(s->objs[0].placeholder == OWF_PH_TITLE && s->objs[1].placeholder == OWF_PH_BODY, "title and body");
    CHECK(s->objs[1].flags & OWF_OBJ_INHERIT_BOX, "a new placeholder takes its layout's box");
    /* the layout's box, even when the slide's copy says otherwise */
    s->objs[1].x = 1;
    n = owf_deck_resolve(d, 0, 1, &box, &levels);
    CHECK(n == OWF_TEXT_LEVELS && levels == d->masters[0].body, "the body's nine levels: %d", n);
    CHECK(box.x == d->layouts[2].objs[1].x && box.width == d->layouts[2].objs[1].width, "the box from the layout");
    n = owf_deck_resolve(d, 0, 0, NULL, &levels);
    CHECK(n == 1 && levels == &d->masters[0].title, "the title's one level");
    CHECK(owf_deck_resolve(d, 0, 9, NULL, NULL) == -1 && owf_deck_resolve(d, 3, 0, NULL, NULL) == -1, "out of range");
    /* levels */
    add_text(&s->objs[1], "a", 1, 0);
    add_text(&s->objs[1], "b", 3, 0);
    for (i = 0; i < 40; i++)
        add_text(&s->objs[1], "more", i % 4, 0);
    CHECK(owf_obj_level(&s->objs[1], 0) == 1 && owf_obj_level(&s->objs[1], 1) == 3 &&
          owf_obj_level(&s->objs[1], 41) == 3, "levels kept as the story grows");
    CHECK(owf_obj_set_level(&s->objs[1], 0, 10) == -1 && owf_obj_set_level(&s->objs[1], 999, 1) == -1, "bad levels refused");
    CHECK(owf_deck_para_level(d, &d->masters[0], &s->objs[1], 1) == &d->masters[0].body[2], "level 3's look");
    /* slides moved and deleted */
    owf_deck_add_slide(d, -1, 0);
    s = owf_deck_add_slide(d, 0, 1);
    CHECK(d->nslides == 3 && d->slides[0].layout == 1 && d->slides[1].layout == 2, "inserted at the front");
    CHECK(owf_deck_move_slide(d, 0, 2) == 0 && d->slides[2].layout == 1 && d->slides[0].layout == 2, "moved to the end");
    CHECK(owf_deck_move_slide(d, 2, 0) == 0 && d->slides[0].layout == 1, "and back");
    CHECK(owf_deck_move_slide(d, 0, 3) == -1, "a move out of range is refused");
    CHECK(owf_deck_delete_slide(d, 1) == 0 && d->nslides == 2 && d->slides[1].layout == 0, "deleted");
    /* a group goes with its objects */
    s = &d->slides[1];
    o = owf_slide_add(s, OWF_OBJ_GROUP);
    o = owf_slide_add(s, OWF_OBJ_SHAPE);
    if (o) o->parent = 0;
    o = owf_slide_add(s, OWF_OBJ_SHAPE);
    if (o) o->parent = 0;
    o = owf_slide_add(s, OWF_OBJ_TEXT);
    CHECK(s->nobjs == 4, "four objects");
    CHECK(owf_slide_delete(s, 0) == 0 && s->nobjs == 1 && s->objs[0].kind == OWF_OBJ_TEXT && s->objs[0].parent == -1,
          "a group deleted with its objects: %d left", s->nobjs);
    /* kept XML has a limit */
    CHECK(owf_deck_keep(d, OWF_KEPT_OBJECT, "<x/>", 4) == 0, "kept");
    owf_deck_free(d);
    owf_deck_free(NULL);
}

static void test_duplicate(void)
{
    owf_deck *d = build_deck();
    owf_slide *source, *copy;
    owf_obj *o, *c;
    owf_para *p;
    int old_n;
    CHECK(d != NULL, "a deck to duplicate");
    if (!d)
        return;
    source = &d->slides[1];
    owf_deck_set_text(&source->name, "Content slide");
    owf_deck_set_text(&source->footer, "Duplicated footer");
    owf_deck_set_text(&source->date_text, "8 November 2026");
    o = &source->objs[2];
    o->id = 77;
    owf_deck_set_text(&o->xml_id, "shape77");
    owf_deck_set_text(&o->name, "An oval");
    owf_deck_set_text(&o->alt, "Alternative text");
    o->npoints = 2;
    o->points = malloc(4 * sizeof *o->points);
    o->caplevels = 3;
    o->levels = malloc(3);
    o->list = malloc(OWF_TEXT_LEVELS * sizeof *o->list);
    o->ncols = 2;
    o->col_widths = malloc(2 * sizeof *o->col_widths);
    CHECK(o->points && o->levels && o->list && o->col_widths, "duplicate fixture allocations");
    if (!o->points || !o->levels || !o->list || !o->col_widths) {
        owf_deck_free(d);
        return;
    }
    o->points[0] = 11; o->points[1] = 22; o->points[2] = 33; o->points[3] = 44;
    o->levels[0] = 1; o->levels[1] = 2; o->levels[2] = 3;
    memset(o->list, 0, OWF_TEXT_LEVELS * sizeof *o->list);
    o->list[1].bullet = 0x2022;
    o->col_widths[0] = 1234; o->col_widths[1] = 2345;
    p = &source->objs[1].text.paras[0];
    owf_deck_set_text(&p->style, "Body Style");
    owf_deck_set_text(&p->bookmarks, "point1@0-5");
    owf_deck_set_text(&p->runs[0].href, "https://example.invalid/");

    old_n = d->nslides;
    copy = owf_deck_duplicate_slide(d, 1, 2);
    CHECK(copy && d->nslides == old_n + 1 && copy == &d->slides[2], "a duplicate inserted after its source");
    if (copy) {
        source = &d->slides[1];
        c = &copy->objs[2];
        CHECK(copy->objs != source->objs && copy->notes.paras != source->notes.paras &&
              copy->name != source->name && copy->footer != source->footer && copy->date_text != source->date_text,
              "slide-owned allocations are independent");
        CHECK(c->points != source->objs[2].points && c->levels != source->objs[2].levels &&
              c->list != source->objs[2].list && c->col_widths != source->objs[2].col_widths &&
              c->name != source->objs[2].name && c->alt != source->objs[2].alt,
              "object-owned allocations are independent");
        CHECK(c->id == 0 && c->xml_id == NULL, "file-local object IDs are not duplicated");
        CHECK(c->points[3] == 44 && c->levels[2] == 3 && c->list[1].bullet == 0x2022 &&
              c->col_widths[1] == 2345 && !strcmp(c->name, "An oval") && !strcmp(c->alt, "Alternative text"),
              "object metadata was copied");
        CHECK(copy->objs[1].text.paras[0].runs != source->objs[1].text.paras[0].runs &&
              copy->objs[1].text.paras[0].runs[0].text != source->objs[1].text.paras[0].runs[0].text &&
              !strcmp(copy->objs[1].text.paras[0].style, "Body Style") &&
              !strcmp(copy->objs[1].text.paras[0].bookmarks, "point1@0-5") &&
              !strcmp(copy->objs[1].text.paras[0].runs[0].href, "https://example.invalid/"),
              "paragraphs, runs, styles, bookmarks and links were copied");
        source->objs[2].points[3] = 99;
        source->objs[1].text.paras[0].runs[0].text[0] = 'X';
        CHECK(c->points[3] == 44 && copy->objs[1].text.paras[0].runs[0].text[0] == 'f',
              "changing the source does not change the duplicate");
    }
    copy = owf_deck_duplicate_slide(d, 1, 0);
    CHECK(copy == &d->slides[0] && copy->name && !strcmp(copy->name, "Content slide") &&
          d->slides[2].name && !strcmp(d->slides[2].name, "Content slide") && copy->objs != d->slides[2].objs,
          "a duplicate inserted before its source is independent too");
    CHECK(owf_deck_duplicate_slide(d, -1, 0) == NULL && owf_deck_duplicate_slide(d, 99, 0) == NULL,
          "bad duplicate sources are refused");
    owf_deck_free(d);
}

static void compare_decks(const char *what, const owf_deck *a, const owf_deck *b)
{
    int i, j, k;
    CHECK(a->nslides == b->nslides && a->width == b->width && a->height == b->height, "%s: slides and size", what);
    CHECK(b->res->title && !strcmp(b->res->title, "Built in C"), "%s: title", what);
    for (i = 0; i < a->nslides && i < b->nslides; i++) {
        const owf_slide *x = &a->slides[i], *y = &b->slides[i];
        CHECK(x->nobjs == y->nobjs, "%s: slide %d objects %d %d", what, i, x->nobjs, y->nobjs);
        CHECK(x->hidden == y->hidden && x->show == y->show, "%s: slide %d hidden, show", what, i);
        CHECK(x->transition.kind == y->transition.kind && x->transition.direction == y->transition.direction &&
              x->transition.advance_after == y->transition.advance_after, "%s: slide %d transition", what, i);
        CHECK((!x->footer && !y->footer) || (x->footer && y->footer && !strcmp(x->footer, y->footer)), "%s: footer", what);
        CHECK(x->notes.nparas == y->notes.nparas, "%s: notes", what);
        for (j = 0; j < x->nobjs && j < y->nobjs; j++) {
            const owf_obj *p = &x->objs[j], *q = &y->objs[j];
            CHECK(p->kind == q->kind && p->placeholder == q->placeholder && p->x == q->x && p->y == q->y &&
                  p->width == q->width && p->height == q->height, "%s: slide %d object %d kind and box", what, i, j);
            CHECK(p->look.fill == q->look.fill && p->look.line == q->look.line && p->look.line_width == q->look.line_width,
                  "%s: slide %d object %d look", what, i, j);
            CHECK(p->shape == q->shape && p->valign == q->valign && !memcmp(p->crop, q->crop, sizeof p->crop),
                  "%s: slide %d object %d shape", what, i, j);
            CHECK(p->text.nparas == q->text.nparas, "%s: slide %d object %d paragraphs %d %d", what, i, j,
                  p->text.nparas, q->text.nparas);
            for (k = 0; k < p->text.nparas && k < q->text.nparas; k++) {
                CHECK(owf_obj_level(p, k) == owf_obj_level(q, k), "%s: %d/%d/%d level", what, i, j, k);
                CHECK(p->text.paras[k].nruns == q->text.paras[k].nruns && (!p->text.paras[k].nruns ||
                      (!strcmp(p->text.paras[k].runs[0].text, q->text.paras[k].runs[0].text) &&
                       p->text.paras[k].runs[0].fmt.flags == q->text.paras[k].runs[0].fmt.flags)),
                      "%s: %d/%d/%d runs", what, i, j, k);
            }
        }
    }
}

static void test_odp(void)
{
    owf_deck *d = build_deck(), *back = NULL;
    unsigned char *data = NULL;
    size_t length = 0;
    int r, i;
    const owf_deck_format *f;

    CHECK(d && d->nslides == 3, "a deck built");
    if (!d)
        return;
    f = owf_deck_format_for_filename("Talk.odp");
    CHECK(f && !strcmp(f->name, "odp"), "odp by its extension");
    CHECK(owf_deck_format_for_filename("Talk.otp") == f, "otp too");
    CHECK(owf_deck_format_for_filename("Talk.FODP") == owf_deck_format_named("fodp"), "fodp, any case");
    CHECK(!owf_deck_format_for_filename("Talk.odt") && !owf_deck_format_for_filename("Talk"), "not a deck's");
    for (i = 0; i < 2; i++) {
        const char *name = i ? "fodp" : "odp";
        f = owf_deck_format_named(name);
        r = f->export(d, &data, &length, NULL);
        CHECK(r == OWF_OK && length > 100, "%s written: %d", name, r);
        if (r != OWF_OK)
            continue;
        CHECK(owf_deck_format_detect(data, length) == f, "%s detected", name);
        r = owf_import_deck_memory(data, length, NULL, &back, NULL, NULL);
        CHECK(r == OWF_OK && back, "%s read back: %d", name, r);
        if (back)
            compare_decks(name, d, back);
        owf_deck_free(back);
        back = NULL;
        free(data);
    }
    /* not a deck */
    r = owf_import_deck_memory((const unsigned char *)"hello", 5, NULL, &back, NULL, NULL);
    CHECK(r == OWF_ERR_FORMAT && !back, "text is not a deck");
    owf_deck_free(d);
}

/* Bytes in bytes (a PDF has NULs in its pictures). */
static const char *find(const unsigned char *data, size_t n, const char *what)
{
    size_t k = strlen(what), i;
    for (i = 0; k && i + k <= n; i++)
        if (data[i] == (unsigned char)what[0] && !memcmp(data + i, what, k))
            return (const char *)data + i;
    return NULL;
}

/* Every object where the cross-reference table says it is. */
static int xref_ok(const unsigned char *pdf, size_t n)
{
    const char *x = NULL, *p, *q;
    int count, i;
    unsigned long at;
    for (p = (const char *)pdf; (q = find((const unsigned char *)p, n - (size_t)(p - (const char *)pdf), "\nxref\n"));
         p = q + 1)
        x = q;
    if (!x || sscanf(x + 6, "0 %d", &count) != 1)
        return 0;
    p = strchr(x + 6, '\n') + 1 + 20;      /* past object 0's line */
    for (i = 1; i < count; i++, p += 20) {
        char want[32];
        if (sscanf(p, "%10lu", &at) != 1 || at >= n)
            return 0;
        snprintf(want, sizeof want, "%d 0 obj", i);
        if (n - at < strlen(want) || memcmp(pdf + at, want, strlen(want)))
            return 0;
    }
    return 1;
}

static void test_pdf_writer(void)
{
    static const unsigned char indexed_png_without_palette[] = {
        0x89, 'P', 'N', 'G', 13, 10, 26, 10,
        0, 0, 0, 13, 'I', 'H', 'D', 'R',
        0, 0, 0, 1, 0, 0, 0, 1, 8, 3, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 10, 'I', 'D', 'A', 'T',
        0x78, 0x9C, 0x63, 0x60, 0, 0, 0, 2, 0, 1, 0, 0, 0, 0,
        0, 0, 0, 0, 'I', 'E', 'N', 'D', 0, 0, 0, 0
    };
    owf_pdf *pdf = owf_pdf_new();
    unsigned char *data = NULL;
    size_t length = 0;
    int pts[6] = { 100, 100, 1000, 100, 500, 900 }, clip[4] = { 0, 0, 500, 500 };
    owf_image im, bad_png;
    int r;

    CHECK(owf_pdf_text_width("Hello", 5, OWF_FONT_SANS, 0, 240) == ((722 + 556 + 222 + 222 + 556) * 240 + 500) / 1000,
          "Helvetica's widths: %d", owf_pdf_text_width("Hello", 5, OWF_FONT_SANS, 0, 240));
    CHECK(owf_pdf_text_width("ii", 2, OWF_FONT_MONO, OWF_BOLD, 200) == 240, "Courier: 600 each");
    CHECK(owf_pdf_winansi(0x20AC) == 0x80 && owf_pdf_winansi(0x25CF) == 0x95 && owf_pdf_winansi(0x4E00) == 0,
          "WinAnsi: euro, bullets, none for CJK");
    CHECK(pdf != NULL, "owf_pdf_new");
    if (!pdf)
        return;
    memset(&im, 0, sizeof im);
    im.data = (unsigned char *)"not a picture";
    im.length = 13;
    memset(&bad_png, 0, sizeof bad_png);
    bad_png.data = (unsigned char *)indexed_png_without_palette;
    bad_png.length = sizeof indexed_png_without_palette;
    owf_pdf_page(pdf, 11906, 16838);
    owf_pdf_text(pdf, 1440, 1440, 3000, "Caf\xc3\xa9 (\xe2\x82\xac) \\", 13, OWF_FONT_SERIF, OWF_BOLD | OWF_UNDERLINE, 240, 0x112233);
    owf_pdf_rect(pdf, 100, 100, 500, 500, OWF_SHADE(0xFF0000), OWF_SHADE(0), 20);
    owf_pdf_round_rect(pdf, 100, 700, 500, 300, 80, OWF_SHADE(0x00FF00), 0, 0);
    owf_pdf_ellipse(pdf, 700, 700, 500, 300, 0, OWF_SHADE(0x0000FF), 0);
    owf_pdf_dash(pdf, 1);
    owf_pdf_path(pdf, pts, 3, 1, OWF_SHADE(0xFFFF00), OWF_SHADE(0), 10);
    owf_pdf_dash(pdf, 0);
    owf_pdf_image(pdf, &im, 0, 0, 100, 100, clip);
    owf_pdf_image(pdf, &bad_png, 0, 0, 100, 100, clip);
    owf_pdf_link(pdf, 1440, 1200, 3000, 300, "https://example.org/(x)");
    owf_pdf_page(pdf, 14400, 10800);
    owf_pdf_text(pdf, 0, 500, 0, "page two", 8, OWF_FONT_MONO, 0, 200, 0);
    CHECK(owf_pdf_page_count(pdf) == 2, "two pages");
    r = owf_pdf_finish(pdf, "T\xc3\xadtulo", &data, &length, NULL);
    CHECK(r == OWF_OK && length > 500 && !memcmp(data, "%PDF-1.4", 8), "a PDF: %d", r);
    if (r == OWF_OK) {
        CHECK(xref_ok(data, length), "objects where the xref says");
        CHECK(find(data, length, "(Caf\\351 \\(\\200\\) \\\\) Tj") != NULL, "text escaped in WinAnsi");
        CHECK(find(data, length, "/MediaBox [0 0 720 540]") != NULL, "a page's size");
        CHECK(find(data, length, "/URI (https://example.org/\\(x\\))") != NULL, "a link");
        CHECK(find(data, length, "/Title <FEFF005400ED00740075006C006F>") != NULL, "title as UTF-16");
        CHECK(!find(data, length, "/Im1 Do"), "a picture it can't hold is left out");
        CHECK(!find(data, length, "/Subtype /Image"), "an indexed PNG without a palette is left out");
    }
    free(data);
    owf_pdf_free(pdf);
}

#ifdef OPR_WITH_LAYOUT
static void test_shape_layout(void)
{
    owf_deck *d = owf_deck_new(10000, 8000);
    owf_slide *slide;
    opr_slide *laid;
    unsigned char *pdf = NULL;
    size_t length = 0, i;
    int kind, shapes = 0, rects = 0, rounds = 0, ellipses = 0, lines = 0, polygons = 0;
    static const int polygon[] = {0, 1000, 500, 0, 1000, 1000};
    CHECK(d != NULL, "shape deck");
    if (!d) return;
    slide = owf_deck_add_slide(d, -1, 0);
    CHECK(slide != NULL, "shape slide");
    if (!slide) { owf_deck_free(d); return; }
    for (kind = OWF_SHAPE_RECT; kind <= OWF_SHAPE_POLYGON; kind++) {
        owf_obj *o = owf_slide_add(slide, OWF_OBJ_SHAPE);
        CHECK(o != NULL, "shape %d added", kind);
        if (!o) continue;
        o->shape = (owf_shape)kind;
        o->x = 500 + kind * 1000;
        o->y = 500;
        o->width = o->height = 800;
        o->look.fill = OWF_SHADE(0x224466 + kind);
        o->look.line = OWF_SHADE(0x101010);
        o->look.line_width = 20;
        if (kind == OWF_SHAPE_RECT) o->rotation = 900;
        if (kind == OWF_SHAPE_ROUNDRECT) o->radius = 200;
        if (kind == OWF_SHAPE_LINE) {
            o->flags |= OWF_OBJ_FLIP_H;
            o->look.fill = 0;
            o->look.line_style = OWF_LINE_DASH_DOT;
            o->look.arrows = OWF_ARROW_START | OWF_ARROW_END;
        }
        if (kind == OWF_SHAPE_POLYGON) {
            o->points = malloc(sizeof polygon);
            if (o->points) {
                memcpy(o->points, polygon, sizeof polygon);
                o->npoints = 3;
            }
        }
    }
    laid = opr_slide_layout(d, 0, NULL, NULL, NULL);
    CHECK(laid != NULL, "all shapes laid out");
    if (laid) {
        for (i = 0; i < ol_display_count(laid->doc); i++) {
            const ol_display_op *op = ol_display_get(laid->doc, i);
            if (!op || op->type != OL_OP_SHAPE || !op->graphic) continue;
            shapes++;
            switch (op->graphic->shape.type) {
            case OL_SHAPE_RECT:
                rects++;
                CHECK(op->graphic->shape.rotation_tenths == 900,
                      "shape rotation carried by OpenLayout");
                break;
            case OL_SHAPE_ROUND_RECT:
                rounds++;
                CHECK(op->graphic->shape.radius_permille == 250, "round radius normalized");
                break;
            case OL_SHAPE_ELLIPSE: ellipses++; break;
            case OL_SHAPE_LINE:
                lines++;
                CHECK(op->graphic->shape.stroke_style == OL_STROKE_DASH_DOT &&
                      op->graphic->shape.flags == (OL_SHAPE_ARROW_START | OL_SHAPE_ARROW_END) &&
                      op->graphic->point_count == 2 && op->graphic->points[0].x == OL_SHAPE_COORDS,
                      "line dash, arrows and flip normalized");
                break;
            case OL_SHAPE_POLYGON: polygons++; break;
            default: break;
            }
        }
        CHECK(shapes == 8 && rects == 1 && rounds == 1 && ellipses == 1 && lines == 1 && polygons == 4,
              "all deck shapes use OpenLayout: %d %d %d %d %d %d",
              shapes, rects, rounds, ellipses, lines, polygons);
        opr_slide_free(laid);
    }
    CHECK(opr_export_pdf(d, NULL, &pdf, &length, NULL) == OWF_OK && xref_ok(pdf, length),
          "shared shapes rendered into PDF");
    free(pdf);
    owf_deck_free(d);
}

static void test_layout(void)
{
    owf_deck *d = build_deck();
    opr_slide *s;
    unsigned char *data = NULL;
    size_t length = 0, i, n;
    int r, title = -1, body = -1, k;
    ol_unit title_top = 0, title_bottom = 0, title_left = 0, title_right = 0, bullet_x = -1, text_x = -1;
    owf_obj *o;

    if (!d)
        return;
    s = opr_slide_layout(d, 0, NULL, NULL, "1/1/2026");
    CHECK(s != NULL, "slide 1 laid out");
    if (s) {
        CHECK(ol_page_count(s->doc) == 1 && !s->overflow, "one page");
        /* the background, two placeholders, the footer and number */
        CHECK(s->nframes == 5, "frames: %d", s->nframes);
        for (k = 0; k < s->nframes; k++)
            if (s->frames[k].obj && s->frames[k].obj->placeholder == OWF_PH_TITLE && !s->frames[k].from_master)
                title = k;
        CHECK(title >= 0, "the title's frame");
        n = ol_display_count(s->doc);
        for (i = 0; i < n && title >= 0; i++) {
            const ol_display_op *op = ol_display_get(s->doc, i);
            if (op->type == OL_OP_TEXT && opr_slide_frame_of(s, op) == title) {
                if (title_bottom == 0)
                    title_left = op->u.text.bounds.x;
                title_top = op->u.text.bounds.y;
                title_bottom = op->u.text.bounds.y + op->u.text.bounds.height;
                title_right = op->u.text.bounds.x + op->u.text.bounds.width;
            }
        }
        if (title >= 0) {
            const owf_obj *b = &s->frames[title].box;
            ol_unit mid = (title_top + title_bottom) / 2, box_mid = (b->y + b->height / 2) * OL_UNITS_PER_TWIP;
            ol_unit centre = (title_left + title_right) / 2, box_centre = (b->x + b->width / 2) * OL_UNITS_PER_TWIP;
            CHECK(title_bottom > title_top && mid > box_mid - 400 && mid < box_mid + 400,
                  "the title in the middle of its box: %d, %d", (int)mid, (int)box_mid);
            CHECK(centre > box_centre - 400 && centre < box_centre + 400, "the title centred: %d, %d", (int)centre,
                  (int)box_centre);
        }
        opr_slide_free(s);
    }
    for (k = 0; k < d->slides[1].nobjs; ++k)
        if (d->slides[1].objs[k].kind == OWF_OBJ_PICTURE) {
            d->slides[1].objs[k].crop[0] = 100;
            d->slides[1].objs[k].crop[1] = 50;
            d->slides[1].objs[k].crop[2] = 200;
            d->slides[1].objs[k].crop[3] = 150;
        }
    s = opr_slide_layout(d, 1, NULL, NULL, NULL);
    CHECK(s != NULL, "slide 2 laid out");
    if (s) {
        const ol_display_op *cropped = NULL;
        const ol_display_op *shape = NULL;
        for (k = 0; k < s->nframes; k++)
            if (s->frames[k].obj && s->frames[k].obj->placeholder == OWF_PH_BODY)
                body = k;
        n = ol_display_count(s->doc);
        for (i = 0; i < n && body >= 0; i++) {
            const ol_display_op *op = ol_display_get(s->doc, i);
            const ol_node *node;
            const char *t;
            if (op->type == OL_OP_IMAGE && op->graphic && op->graphic->crop.left == 100)
                cropped = op;
            if (op->type == OL_OP_SHAPE && op->graphic && op->graphic->shape.type == OL_SHAPE_ELLIPSE)
                shape = op;
            if (op->type != OL_OP_TEXT || opr_slide_frame_of(s, op) != body)
                continue;
            node = ol_node_by_id(s->doc, op->u.text.node_id);
            t = ol_node_text(node) + op->u.text.text_offset;
            if (!strncmp(t, "\xe2\x97\x8f", 3) && bullet_x < 0)
                bullet_x = op->u.text.bounds.x;
            if (!strncmp(t, "first", 5))
                text_x = op->u.text.bounds.x;
        }
        CHECK(bullet_x >= 0 && text_x > bullet_x, "a bullet before its text: %d %d", (int)bullet_x, (int)text_x);
        CHECK(cropped && cropped->graphic->crop.top == 50 && cropped->graphic->crop.right == 200 &&
              cropped->graphic->crop.bottom == 150 && cropped->clip.width == OL_TWIP(1440) &&
              cropped->clip.height == OL_TWIP(1440), "picture crop and clip are in OpenLayout");
        CHECK(shape && shape->graphic->shape.fill_argb == 0xff336699u &&
              shape->graphic->shape.stroke_argb == 0xff000000u &&
              shape->graphic->shape.stroke_width == OL_TWIP(20) &&
              shape->u.fill_rect.bounds.x == OL_TWIP(12000) &&
              shape->u.fill_rect.bounds.width == OL_TWIP(3000),
              "ellipse geometry and look are in OpenLayout");
        opr_slide_free(s);
    }
    /* text that doesn't fit shrinks */
    o = &d->slides[1].objs[1];
    o->autofit = OWF_AUTOFIT_SHRINK;
    for (k = 0; k < 30; k++)
        add_text(o, "a line among many that cannot all fit in the box", 1, 0);
    s = opr_slide_layout(d, 1, NULL, NULL, NULL);
    if (s) {
        for (k = 0; k < s->nframes; k++)
            if (s->frames[k].obj == o)
                CHECK(s->frames[k].scale < 100, "shrunk to %d%%", s->frames[k].scale);
        opr_slide_free(s);
    }
    r = opr_export_pdf(d, NULL, &data, &length, NULL);
    CHECK(r == OWF_OK && xref_ok(data, length), "the deck's PDF: %d", r);
    if (getenv("TEST_DECK_PDF")) {
        FILE *f = fopen(getenv("TEST_DECK_PDF"), "wb");
        if (f) {
            fwrite(data, 1, length, f);
            fclose(f);
        }
    }
    if (r == OWF_OK) {
        CHECK(find(data, length, "/Count 2 ") != NULL, "two pages: the hidden slide left out");
        CHECK(find(data, length, "(deck) Tj") && find(data, length, "(from) Tj"), "the title's text");
        CHECK(find(data, length, "(Footer) Tj") != NULL, "the footer");
        CHECK(find(data, length, "/Subtype /Image") != NULL, "the picture");
    }
    free(data);
    {
        opr_pdf_options opt;
        memset(&opt, 0, sizeof opt);
        opt.with_hidden = 1;
        r = opr_export_pdf(d, &opt, &data, &length, NULL);
        CHECK(r == OWF_OK && find(data, length, "/Count 3 ") != NULL, "with the hidden slide: three pages");
        free(data);
    }
    owf_deck_free(d);
}
#endif

int main(void)
{
    test_model();
    test_duplicate();
    test_odp();
    test_pdf_writer();
#ifdef OPR_WITH_LAYOUT
    test_shape_layout();
    test_layout();
#else
    printf("test_deck: built without OpenLayout: the layout and PDF tests were skipped\n");
#endif
    printf("test_deck: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
