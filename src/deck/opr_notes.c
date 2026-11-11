/*
 * GFOpenPresentation's core: notes pages and handouts
 * (openpresentation_core.h, "Notes pages and handouts"). Where the slide
 * and its notes go on a notes page, where each slide goes on a handout
 * page, and the notes' text as one string. The notes page itself is laid
 * out in opr_layout.c (opr_notes_layout), with the slides' builder.
 *
 * Plain C99, no floating point. MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "openpresentation_core.h"

#include <stdlib.h>
#include <string.h>

#define A4_WIDTH 11906
#define A4_HEIGHT 16838
#define MARGIN 1134              /* 2 cm */
#define GAP 567                  /* 1 cm between the slide and the notes, and between handout slides */

void opr_notes_page(const owf_deck *deck, int *width, int *height)
{
    int w = deck ? deck->notes_page.width : 0, h = deck ? deck->notes_page.height : 0;
    if (w <= 0 || h <= 0) {
        w = A4_WIDTH;
        h = A4_HEIGHT;
    }
    if (width) *width = w;
    if (height) *height = h;
}

/* The page's margins, each one a margin unless the page leaves no room. */
static void page_area(const owf_deck *deck, int area[4])
{
    int w, h, l, t, r, b;
    opr_notes_page(deck, &w, &h);
    l = deck && deck->notes_page.margin_left > 0 ? deck->notes_page.margin_left : MARGIN;
    t = deck && deck->notes_page.margin_top > 0 ? deck->notes_page.margin_top : MARGIN;
    r = deck && deck->notes_page.margin_right > 0 ? deck->notes_page.margin_right : MARGIN;
    b = deck && deck->notes_page.margin_bottom > 0 ? deck->notes_page.margin_bottom : MARGIN;
    if (l + r > w / 2) l = r = w / 8;
    if (t + b > h / 2) t = b = h / 8;
    area[0] = l;
    area[1] = t;
    area[2] = w - l - r;
    area[3] = h - t - b;
}

/* The slide's shape fitted in w x h, centred there. */
static void fit_slide(const owf_deck *deck, int x, int y, int w, int h, int box[4])
{
    long sw = deck && deck->width > 0 ? deck->width : 4, sh = deck && deck->height > 0 ? deck->height : 3;
    long fw = w, fh = (long)w * sh / sw;
    if (fh > h) {
        fh = h;
        fw = (long)h * sw / sh;
    }
    box[0] = x + (int)((w - fw) / 2);
    box[1] = y + (int)((h - fh) / 2);
    box[2] = (int)fw;
    box[3] = (int)fh;
}

void opr_notes_boxes(const owf_deck *deck, int slide_box[4], int text_box[4])
{
    int area[4], s[4];
    /* the slide in the top half at most, as wide as the margins let it */
    page_area(deck, area);
    fit_slide(deck, area[0], area[1], area[2], (area[3] - GAP) / 2, s);
    s[1] = area[1];              /* at the top, not in the middle of its half */
    if (slide_box)
        memcpy(slide_box, s, sizeof s);
    if (text_box) {
        text_box[0] = area[0];
        text_box[1] = s[1] + s[3] + GAP;
        text_box[2] = area[2];
        text_box[3] = area[1] + area[3] - text_box[1];
    }
}

int opr_handout_box(const owf_deck *deck, int per_page, int k, int box[4])
{
    int area[4], cols, rows, cw, ch;
    switch (per_page) {
    case 1: cols = 1; rows = 1; break;
    case 2: cols = 1; rows = 2; break;
    case 3: cols = 1; rows = 3; break;
    case 4: cols = 2; rows = 2; break;
    case 9: cols = 3; rows = 3; break;
    default: per_page = 6; cols = 2; rows = 3; break;
    }
    if (k < 0 || k >= per_page || !box)
        return -1;
    page_area(deck, area);
    cw = (area[2] - (cols - 1) * GAP) / cols;
    ch = (area[3] - (rows - 1) * GAP) / rows;
    fit_slide(deck, area[0] + (k % cols) * (cw + GAP), area[1] + (k / cols) * (ch + GAP), cw, ch, box);
    return 0;
}

char *opr_notes_get(const owf_deck *deck, int slide)
{
    const owf_obj *o;
    size_t total = 1, at = 0;
    char *out;
    int i;
    if (!deck || slide < 0 || slide >= deck->nslides)
        return NULL;
    o = &deck->slides[slide].notes;
    for (i = 0; i < o->text.nparas; i++) {
        size_t n;
        char *t = opr_para_text(&o->text.paras[i], &n);
        if (!t)
            return NULL;
        total += n + 1;
        owf_free(t);
    }
    out = owf_malloc(total);
    if (!out)
        return NULL;
    for (i = 0; i < o->text.nparas; i++) {
        size_t n;
        char *t = opr_para_text(&o->text.paras[i], &n);
        if (!t) {
            owf_free(out);
            return NULL;
        }
        if (i)
            out[at++] = '\n';
        memcpy(out + at, t, n);
        at += n;
        owf_free(t);
    }
    out[at] = 0;
    return out;
}

int opr_notes_set(opr_undo *u, owf_deck *deck, int slide, const char *utf8)
{
    if (!deck || slide < 0 || slide >= deck->nslides || !utf8)
        return -1;
    if (u && opr_undo_push(u, deck, slide, OPR_NOTES) < 0)
        return -1;
    /* run out of memory part way: the step put back what was there */
    return opr_text_set(deck, slide, OPR_NOTES, utf8) < 0 ? -1 : 0;
}
