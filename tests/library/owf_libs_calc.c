/*
 * OWFLibsTest: owfcalc.library (built with OWF_USE_CALC_LIBRARY): the
 * shared calls, then owf_lib_checks.c's workbook and engine checks
 * (owf_lib_book_checks: that file built with OWF_LIB_CHECKS_PART 1 for this
 * library): the fixture workbook as XLS, XLSX and ODS, saved and read back,
 * and the formula engine on a new book, through the library's own maths.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "owf.h"
#include "owf_open.h"
#include "owf_lib_checks.h"
#include "owf_libs_shared.h"

static void run(const owf_libs_env *e)
{
    owf_lib_checks c;
    shared_checks(e, OWF_KIND_UNKNOWN);
    c.check = e->check;
    c.ctx = e->ctx;
    c.read_file = unit_read_file;
    c.delete_file = unit_delete_file;
    c.fixtures = e->fixtures;
    c.temp = e->temp;
    c.tag = e->tag;
    owf_lib_book_checks(&c);
}

OWF_LIBS_UNIT(owf_libs_calc, run);
