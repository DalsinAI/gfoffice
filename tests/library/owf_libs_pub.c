/*
 * OWFLibsTest: owfpub.library (built with OWF_USE_PUB_LIBRARY): the shared
 * calls, Publisher's detector, then owf_lib_checks.c's .pub check
 * (owf_lib_pub_checks: that file built with OWF_LIB_CHECKS_PART 3 for this
 * library): Sample.pub read, its pages and version. Then the 3.8 calls:
 * the templates, a merge table from text, a merge field and Mail Merge.
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
}

OWF_LIBS_UNIT(owf_libs_pub, run);
