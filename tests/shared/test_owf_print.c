/*
 * libowf's print core (owf_print.c, owf_print.h; docs/suite/owf-shared-core.md): the bleed and crop marks'
 * room, the marks' lines, the bleed's colour and a booklet's page order, against the numbers
 * src/deck/opr_pdf.c had of its own before they moved (main 918bec99; tests/deck/test_deck.c checks the
 * same through the opr_ names and the PDF). And libowf's file length (owf_sys_file_size), with the mail
 * merge data file that is refused by it before it is read (opp_merge_load_file).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "owf_print.h"
#include "owf_internal.h"
#include "ow_merge.h"
#include "openpressmaster_merge.h"

#include <stdio.h>
#include <string.h>

static int checks, failed;
#define CHECK(c, ...) do { ++checks; if (!(c)) { ++failed; fprintf(stderr, "%s:%d: ", __FILE__, __LINE__); \
    fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

static void test_room(void)
{
    CHECK(owf_print_bleed(0) == 0 && owf_print_bleed(-5) == 0, "no bleed");
    CHECK(owf_print_bleed(170) == 170, "3 mm: %d", owf_print_bleed(170));
    CHECK(owf_print_bleed(1440) == 1440 && owf_print_bleed(1441) == 1440 && owf_print_bleed(99999) == 1440,
          "the bleed at most an inch");
    CHECK(owf_print_mark_gap(0) == 120 && owf_print_mark_gap(-1) == 120 && owf_print_mark_gap(119) == 120,
          "the least gap");
    CHECK(owf_print_mark_gap(180) == 180 && owf_print_mark_gap(99999) == 1440, "the gap past the bleed");
    CHECK(owf_print_margin(0, 0) == 0 && owf_print_margin(-9, 0) == 0, "no room without marks or bleed");
    CHECK(owf_print_margin(0, 1) == 120 + 360 + 60, "marks alone: %d", owf_print_margin(0, 1));
    CHECK(owf_print_margin(180, 0) == 180, "bleed alone: %d", owf_print_margin(180, 0));
    CHECK(owf_print_margin(180, 1) == 180 + 360 + 60, "marks outside the bleed: %d", owf_print_margin(180, 1));
    CHECK(owf_print_margin(99999, 1) == 1440 + 360 + 60 && owf_print_margin(99999, 0) == 1440, "an inch at most");
    CHECK(owf_print_margin(170, 7) == owf_print_margin(170, 1), "any marks flag");
}

/* An A4 page at (540, 540), the marks 120 from its edges: the lines opr_pdf.c's crop_marks drew, in its order */
static void test_crop_lines(void)
{
    static const int want[32] = {
        420, 540, 60, 540,           540, 420, 540, 60,                /* top left: across, down */
        12566, 540, 12926, 540,      12446, 420, 12446, 60,            /* top right */
        420, 17378, 60, 17378,       540, 17498, 540, 17858,           /* bottom left */
        12566, 17378, 12926, 17378,  12446, 17498, 12446, 17858,       /* bottom right */
    };
    int xy[32], i, same = 1, outside = 1;
    memset(xy, 0x55, sizeof xy);
    CHECK(owf_print_crop_lines(540, 11906, 16838, 120, xy) == OWF_PRINT_CROP_LINES && OWF_PRINT_CROP_LINES == 8,
          "eight lines");
    for (i = 0; i < 32; i++)
        if (xy[i] != want[i]) { same = 0; CHECK(0, "crop line value %d: %d, not %d", i, xy[i], want[i]); }
    CHECK(same, "the marks as they were");
    CHECK(owf_print_crop_lines(540, 11906, 16838, 120, NULL) == -1, "nowhere to put them");
    /* with a 3 mm bleed: every mark stays on the paper and clear of the bleed */
    {
        int bleed = 170, gap = owf_print_mark_gap(bleed), m = owf_print_margin(bleed, 1), w = 5953, h = 8419;
        owf_print_crop_lines(m, w, h, gap, xy);
        for (i = 0; i < 8; i++) {
            int x1 = xy[4 * i], y1 = xy[4 * i + 1], x2 = xy[4 * i + 2], y2 = xy[4 * i + 3];
            int inx = (x1 > m - bleed && x1 < m + w + bleed) || (x2 > m - bleed && x2 < m + w + bleed);
            int iny = (y1 > m - bleed && y1 < m + h + bleed) || (y2 > m - bleed && y2 < m + h + bleed);
            if (inx && iny) outside = 0;
            if (x1 < 0 || x2 < 0 || y1 < 0 || y2 < 0 || x1 > w + 2 * m || x2 > w + 2 * m || y1 > h + 2 * m ||
                y2 > h + 2 * m) outside = 0;
        }
        CHECK(outside, "marks on the paper, outside the bleed");
    }
}

static void test_bleed_colour(void)
{
    owf_deck *d = owf_deck_new(11906, 16838);
    owf_master *m;
    CHECK(d && owf_deck_add_slide(d, -1, -1) && owf_deck_add_slide(d, -1, -1), "two pages");
    if (!d || d->nslides < 2) { owf_deck_free(d); return; }
    CHECK(owf_print_bleed_colour(NULL, 0) == 0 && owf_print_bleed_colour(d, -1) == 0 &&
              owf_print_bleed_colour(d, 2) == 0, "no such page");
    CHECK(owf_print_bleed_colour(d, 0) == 0, "no background: the bleed stays white");
    d->slides[0].background = OWF_SHADE(0x336699);
    CHECK(owf_print_bleed_colour(d, 0) == OWF_SHADE(0x336699), "the page's own colour: %lx",
          owf_print_bleed_colour(d, 0));
    d->slides[0].background = OWF_SHADE(0xFFFFFF);
    CHECK(owf_print_bleed_colour(d, 0) == 0, "white is no bleed colour");
    m = (owf_master *)owf_deck_slide_master(d, 1);
    CHECK(m != NULL, "page 2's master");
    if (m) {
        unsigned long was = m->background;
        m->background = OWF_SHADE(0x102030);
        CHECK(owf_print_bleed_colour(d, 1) == OWF_SHADE(0x102030), "the master's colour: %lx",
              owf_print_bleed_colour(d, 1));
        CHECK(owf_print_bleed_colour(d, 0) == 0, "the page's own white wins over its master");
        d->slides[1].background_image = 0;         /* a picture for a background: the master's colour isn't under it */
        CHECK(owf_print_bleed_colour(d, 1) == 0, "a background picture: no colour");
        d->slides[1].background_image = -1;
        m->background = was;
    }
    owf_deck_free(d);
}

static void test_booklet(void)
{
    static const int eight[4][2] = { { 7, 0 }, { 1, 6 }, { 5, 2 }, { 3, 4 } };
    int k, l, r, n;
    CHECK(owf_print_booklet_sides(0) == 0 && owf_print_booklet_sides(-3) == 0, "no pages, no sides");
    CHECK(owf_print_booklet_sides(1) == 2 && owf_print_booklet_sides(4) == 2 && owf_print_booklet_sides(5) == 4 &&
              owf_print_booklet_sides(8) == 4 && owf_print_booklet_sides(9) == 6, "two sides a sheet of four pages");
    for (k = 0; k < 4; k++)
        CHECK(owf_print_booklet_side(8, k, &l, &r) == 0 && l == eight[k][0] && r == eight[k][1],
              "8 pages, side %d: %d %d", k, l, r);
    CHECK(owf_print_booklet_side(8, 4, &l, &r) == -1 && owf_print_booklet_side(0, 0, &l, &r) == -1 &&
              owf_print_booklet_side(8, -1, &l, &r) == -1, "no such side");
    CHECK(owf_print_booklet_side(5, 0, &l, &r) == 0 && l == -1 && r == 0, "5 pages: the last page blank: %d %d", l, r);
    CHECK(owf_print_booklet_side(5, 1, &l, &r) == 0 && l == 1 && r == -1, "5 pages, side 1: %d %d", l, r);
    CHECK(owf_print_booklet_side(1, 0, &l, &r) == 0 && l == -1 && r == 0 &&
              owf_print_booklet_side(1, 1, &l, &r) == 0 && l == -1 && r == -1, "one page: one sheet, the rest blank");
    CHECK(owf_print_booklet_side(8, 0, NULL, NULL) == 0, "nowhere to answer is allowed");
    /* every page of any booklet is printed once, and each sheet's two sides back each other */
    for (n = 1; n <= 40; n++) {
        int seen[40], sides = owf_print_booklet_sides(n), i, once = 1, total = (n + 3) / 4 * 4;
        memset(seen, 0, sizeof seen);
        for (k = 0; k < sides; k++) {
            if (owf_print_booklet_side(n, k, &l, &r) != 0) { once = 0; break; }
            if (l >= 0) seen[l]++;
            if (r >= 0) seen[r]++;
            /* an odd page number (0-based even) is always a right-hand page */
            if ((l >= 0 && l % 2 == 0) || (r >= 0 && r % 2 == 1)) once = 0;
        }
        for (i = 0; i < n; i++) if (seen[i] != 1) once = 0;
        CHECK(once && sides * 2 == total, "%d pages: each printed once on %d sides", n, sides);
    }
}

static void test_file_size(void)
{
    const char *path = "build/host/test-owf-print-size.csv";
    ow_merge_table *t = (ow_merge_table *)1;
    size_t n = 99;
    FILE *f = fopen(path, "wb");
    CHECK(f != NULL, "a file to measure");
    if (!f) return;
    fputs("Name;Town\r\nJo;Leeds\r\n", f);
    fclose(f);
    CHECK(owf_sys_file_size(path, &n) == OWF_OK && n == 21, "21 bytes: %lu", (unsigned long)n);
    CHECK(opp_merge_load_file(path, &t) == OWF_OK && t && t->nrows == 1, "a small data file is read");
    ow_merge_table_free(t);
    CHECK(owf_sys_file_size(path, NULL) == OWF_ERR_IO && owf_sys_file_size(NULL, &n) == OWF_ERR_IO && n == 0,
          "nothing to measure");
    /* one byte over the limit (the rest a hole, where the file system has them): refused by its length */
    f = fopen(path, "wb");
    CHECK(f != NULL, "a big file");
    if (f) {
        CHECK(fseek(f, (long)OW_MERGE_MAX_BYTES, SEEK_SET) == 0 && fputc('x', f) == 'x', "a byte past the limit");
        fclose(f);
        CHECK(owf_sys_file_size(path, &n) == OWF_OK && n == OW_MERGE_MAX_BYTES + 1, "its length: %lu", (unsigned long)n);
        t = (ow_merge_table *)1;
        CHECK(opp_merge_load_file(path, &t) == OWF_ERR_UNSUPPORTED && t == NULL, "too big for a merge table");
    }
    /* just at the limit: read (and no table in it, being all NULs) */
    f = fopen(path, "wb");
    if (f) {
        CHECK(fseek(f, (long)OW_MERGE_MAX_BYTES - 1, SEEK_SET) == 0 && fputc('x', f) == 'x', "a file at the limit");
        fclose(f);
        int rc;
        t = (ow_merge_table *)1;
        rc = opp_merge_load_file(path, &t);
        CHECK(rc != OWF_ERR_UNSUPPORTED && rc != OWF_ERR_IO, "a file at the limit is read: %d", rc);
        ow_merge_table_free(t);
    }
    remove(path);
    CHECK(owf_sys_file_size(path, &n) == OWF_ERR_IO && n == 0, "no such file");
}

int main(void)
{
    test_room();
    test_crop_lines();
    test_bleed_colour();
    test_booklet();
    test_file_size();
    if (failed) { fprintf(stderr, "print core: %d of %d checks failed\n", failed, checks); return 1; }
    printf("print core tests passed (%d checks)\n", checks);
    return 0;
}
