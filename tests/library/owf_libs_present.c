/*
 * OWFLibsTest: owfpresent.library (built with OWF_USE_PRESENT_LIBRARY): the
 * shared calls, then decks: the fixture ODP opened, saved in T: and read
 * back; a new deck with a slide and a paragraph of text; a page drawn by
 * the page PDF writer, which hands back memory the program frees with
 * owf_free.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "owf.h"
#include "owf_open.h"
#include "owf_deck.h"
#include "owf_pdf.h"
#include "owf_libs_shared.h"

static void fixture_deck(const owf_libs_env *e)
{
    char path[256], out[96];
    owf_deck *deck = NULL, *back = NULL;
    owf_report *report = owf_report_new();
    const owf_deck_format *used = NULL;
    int r, slides;

    unit_path(path, sizeof path, e->fixtures, "libreoffice.odp");
    r = owf_import_deck_file(path, NULL, &deck, report, &used);
    LCHECK(e, r == OWF_OK && deck && deck->nslides > 0 && used && !strcmp(used->name, "odp"),
           "%s: libreoffice.odp opens: %d slide(s)", e->tag, deck ? deck->nslides : 0);
    if (r != OWF_OK || !deck) {
        owf_report_free(report);
        return;
    }
    slides = deck->nslides;
    snprintf(out, sizeof out, "%sowflibstest-%s.odp", e->temp, e->tag);
    for (char *s = out; *s; s++)
        if (*s == ' ') *s = '-';
    r = owf_export_deck_file(deck, out, "odp", report);
    if (r == OWF_OK)
        r = owf_import_deck_file(out, NULL, &back, report, NULL);
    LCHECK(e, r == OWF_OK && back && back->nslides == slides, "%s: saved as ODP and read back: %d slide(s)", e->tag,
           back ? back->nslides : 0);
    unit_delete_file(out);
    owf_deck_free(back);
    owf_deck_free(deck);
    owf_report_free(report);
}

static void new_deck(const owf_libs_env *e)
{
    owf_deck *deck = owf_deck_new(OWF_DECK_4_3_WIDTH, OWF_DECK_4_3_HEIGHT);
    const owf_master *master;
    owf_slide *slide;
    owf_obj *obj;
    owf_para *para;
    owf_charfmt cf;
    int ok;

    slide = deck ? owf_deck_add_slide(deck, -1, -1) : NULL;
    obj = slide ? owf_slide_add(slide, OWF_OBJ_TEXT) : NULL;
    master = deck ? owf_deck_slide_master(deck, 0) : NULL;
    para = obj ? owf_deck_add_para(deck, master, obj, 0) : NULL;
    owf_charfmt_init(&cf);
    ok = para && owf_para_add_text(para, &cf, "Through owfpresent.library", 26) >= 0;
    LCHECK(e, deck && deck->nmasters >= 1 && deck->nlayouts >= 1 && deck->nslides == 1 && master && ok &&
           obj->text.nparas == 1, "%s: a new deck, a slide and its text", e->tag);
    owf_deck_free(deck);
}

static void page_pdf(const owf_libs_env *e)
{
    owf_pdf *pdf = owf_pdf_new();
    owf_report *report = owf_report_new();
    unsigned char *data = NULL;
    size_t length = 0;
    int r = -1, width = owf_pdf_text_width("Slide one", 9, OWF_FONT_SANS, OWF_BOLD, 640);

    if (pdf && owf_pdf_page(pdf, OWF_DECK_4_3_WIDTH, OWF_DECK_4_3_HEIGHT) == 0) {
        owf_pdf_rect(pdf, 720, 720, 12960, 1440, OWF_SHADE(0xDDE4F0), OWF_SHADE(0x203040), 20);
        owf_pdf_text(pdf, 1000, 1700, width, "Slide one", 9, OWF_FONT_SANS, OWF_BOLD, 640, OWF_SHADE(0x000000));
        owf_pdf_ellipse(pdf, 2000, 4000, 3000, 2000, OWF_SHADE(0xF0C000), 0, 0);
        owf_pdf_link(pdf, 720, 720, 2000, 400, "https://amigachrome.com/");
        r = owf_pdf_finish(pdf, "OWFLibsTest", &data, &length, report);
    }
    LCHECK(e, width > 0 && r == OWF_OK && data && length > 400 && !memcmp(data, "%PDF-", 5) &&
           owf_pdf_page_count(pdf) == 1, "%s: a page from the page PDF writer: %lu bytes", e->tag,
           (unsigned long)length);
    owf_free(data);
    owf_pdf_free(pdf);
    owf_report_free(report);
}

static void run(const owf_libs_env *e)
{
    shared_checks(e, OWF_KIND_UNKNOWN);
    LCHECK(e, owf_deck_format_count() >= 2 && owf_deck_format_named("odp") &&
           owf_deck_format_for_filename("Talk.ODP") == owf_deck_format_named("odp"),
           "%s: presentation formats listed, by name and by file name", e->tag);
    fixture_deck(e);
    new_deck(e);
    page_pdf(e);
}

OWF_LIBS_UNIT(owf_libs_present, run);
