/*
 * OWFLibsTest: owfpub.library (built with OWF_USE_PUB_LIBRARY): the shared
 * calls, Publisher's detector, then owf_lib_checks.c's .pub check
 * (owf_lib_pub_checks: that file built with OWF_LIB_CHECKS_PART 3 for this
 * library): Sample.pub read, its pages and version.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "owf.h"
#include "owf_open.h"
#include "owf_lib_checks.h"
#include "owf_libs_shared.h"

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
}

OWF_LIBS_UNIT(owf_libs_pub, run);
