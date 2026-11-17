/*
 * OWFLibsTest: owfpub.library (built with OWF_USE_PUB_LIBRARY): the shared
 * calls, Publisher's detector, then owf_lib_checks.c's .pub check
 * (owf_lib_pub_checks: that file built with OWF_LIB_CHECKS_PART 3 for this
 * library): Sample.pub read, its pages and version. Then the 3.8 calls:
 * the templates, a merge table from text, a merge field and Mail Merge.
 * Then the 3.10 calls: the print core's bleed, crop marks and booklet order.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "owf.h"
#include "owf_open.h"
#include "owf_lib_checks.h"
#include "owf_libs_shared.h"
#include "owf_deck.h"
#include "ow_merge.h"
#include "openpressmaster_merge.h"
#include "openpressmaster_template.h"
#include "owf_print.h"

/* owfpub.library 3.8: the templates and Mail Merge on a Flyer (its frame,
   object 1, takes a Name field; two records make two pages). */
static void merge_checks(const owf_libs_env *e)
{
    static const char csv[] = "Name,Town\nJo,Leeds\nAl,York\n";
    const opp_template_info *info;
    ow_merge_table *t;
    owf_deck *d = NULL;
    int flyer, memory = 0, missing = -1;

    flyer = opp_template_find("flyer");
    info = opp_template_info_at(flyer);
    LCHECK(e, opp_template_count() >= 8 && flyer >= 0 && info && !strcmp(info->name, "Flyer") &&
                  !opp_template_info_at(opp_template_count()) && opp_template_find("No Such") == -1,
           "%s: opp_template_count/find/info_at", e->tag);
    LCHECK(e, opp_template_new(flyer, &d) == OWF_OK && d && d->nslides == 1 && d->slides[0].nobjs >= 2,
           "%s: opp_template_new", e->tag);
    t = ow_merge_table_from_text(csv, sizeof csv - 1, &memory);
    LCHECK(e, t && ow_merge_column(t, "Town") == 1 && ow_merge_column(t, "None") < 0 && ow_merge_value(t, 1, "Town") &&
                  !strcmp(ow_merge_value(t, 1, "Town"), "York"),
           "%s: ow_merge_table_from_text/column/value", e->tag);
    if (d && t && d->nslides == 1 && d->slides[0].nobjs >= 2) {
        LCHECK(e, opp_merge_insert_field_at(&d->slides[0].objs[1], 0, 0, "Name") == OWF_OK &&
                      opp_merge_fields(d, t, &missing) == 1 && missing == 0,
               "%s: opp_merge_insert_field_at/fields", e->tag);
        LCHECK(e, opp_merge_pages(d, t, -1, -1) == OWF_OK && d->nslides == 2, "%s: opp_merge_pages", e->tag);
    }
    ow_merge_table_free(t);
    owf_deck_free(d);
}

/* owfpub.library 3.10: the print core. A 3 mm bleed with crop marks round an A5 page, the page's colour into
   the bleed, and an eight-page booklet's first two sides. */
static void print_checks(const owf_libs_env *e)
{
    owf_deck *d = NULL;
    int xy[4 * OWF_PRINT_CROP_LINES], m, l = -2, r = -2, flyer = opp_template_find("flyer");

    LCHECK(e, owf_print_bleed(170) == 170 && owf_print_bleed(99999) == OWF_PRINT_MAX_BLEED && owf_print_bleed(-1) == 0 &&
                  owf_print_mark_gap(0) == OWF_PRINT_MARK_GAP && owf_print_mark_gap(170) == 170,
           "%s: owf_print_bleed/mark_gap", e->tag);
    m = owf_print_margin(170, 1);
    LCHECK(e, m == 170 + OWF_PRINT_MARK_LENGTH + OWF_PRINT_MARK_ROOM && owf_print_margin(170, 0) == 170 &&
                  owf_print_margin(0, 0) == 0,
           "%s: owf_print_margin", e->tag);
    memset(xy, 0, sizeof xy);
    LCHECK(e, owf_print_crop_lines(m, 5953, 8419, owf_print_mark_gap(170), xy) == OWF_PRINT_CROP_LINES &&
                  xy[0] == m - 170 && xy[1] == m && xy[2] == m - 170 - OWF_PRINT_MARK_LENGTH && xy[3] == m &&
                  xy[28] == m + 5953 && xy[29] == m + 8419 + 170 && xy[30] == m + 5953 &&
                  xy[31] == m + 8419 + 170 + OWF_PRINT_MARK_LENGTH && owf_print_crop_lines(m, 5953, 8419, 170, NULL) == -1,
           "%s: owf_print_crop_lines", e->tag);
    LCHECK(e, owf_print_booklet_sides(8) == 4 && owf_print_booklet_sides(5) == 4 && owf_print_booklet_sides(0) == 0 &&
                  owf_print_booklet_side(8, 0, &l, &r) == 0 && l == 7 && r == 0 &&
                  owf_print_booklet_side(8, 1, &l, &r) == 0 && l == 1 && r == 6 &&
                  owf_print_booklet_side(5, 0, &l, &r) == 0 && l == -1 && r == 0 &&
                  owf_print_booklet_side(8, 4, &l, &r) == -1,
           "%s: owf_print_booklet_sides/side", e->tag);
    if (flyer >= 0 && opp_template_new(flyer, &d) == OWF_OK && d && d->nslides >= 1) {
        d->slides[0].background = OWF_SHADE(0x336699);
        LCHECK(e, owf_print_bleed_colour(d, 0) == OWF_SHADE(0x336699) && owf_print_bleed_colour(d, d->nslides) == 0 &&
                      owf_print_bleed_colour(NULL, 0) == 0,
               "%s: owf_print_bleed_colour", e->tag);
        d->slides[0].background = OWF_SHADE(0xFFFFFF);
        LCHECK(e, owf_print_bleed_colour(d, 0) == 0, "%s: owf_print_bleed_colour, a white page", e->tag);
    }
    owf_deck_free(d);
}

static void run(const owf_libs_env *e)
{
    owf_lib_checks c;
    char path[256];
    unsigned char *data;
    size_t length;

    shared_checks(e, OWF_KIND_UNKNOWN);
    unit_path(path, sizeof path, e->fixtures, "Sample.pub");
    if (unit_read_file(path, &data, &length)) {
        LCHECK(e, owf_pub_detect(data, length) > 0 && owf_pub_detect(data, 100) == 0, "%s: Sample.pub detected",
               e->tag);
        owf_free(data);
    } else {
        LCHECK(e, 0, "%s: Sample.pub read", e->tag);
    }
    LCHECK(e, owf_pub_version_name(OWF_PUB_2007) && !strcmp(owf_pub_version_name(OWF_PUB_2007), "Publisher 2007"),
           "%s: owf_pub_version_name", e->tag);
    c.check = e->check;
    c.ctx = e->ctx;
    c.read_file = unit_read_file;
    c.delete_file = unit_delete_file;
    c.fixtures = e->fixtures;
    c.temp = e->temp;
    c.tag = e->tag;
    owf_lib_pub_checks(&c);
    merge_checks(e);
    print_checks(e);
}

OWF_LIBS_UNIT(owf_libs_pub, run);
