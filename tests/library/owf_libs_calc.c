/*
 * OWFLibsTest: owfcalc.library (built with OWF_USE_CALC_LIBRARY): the
 * shared calls, then owf_lib_checks.c's workbook and engine checks
 * (owf_lib_book_checks: that file built with OWF_LIB_CHECKS_PART 1 for this
 * library): the fixture workbook as XLS, XLSX and ODS, saved and read back,
 * and the formula engine on a new book, through the library's own maths;
 * since 3.3, a printed-sheet page through the shared PDF writer too.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "owf.h"
#include "owf_open.h"
#include "owf_pdf.h"
#include "owf_lib_checks.h"
#include "owf_libs_shared.h"

static void run(const owf_libs_env *e)
{
    owf_lib_checks c;
    owf_pdf *pdf;
    unsigned char *data = NULL;
    size_t length = 0;
    int r = -1;
    shared_checks(e, OWF_KIND_UNKNOWN);
    c.check = e->check;
    c.ctx = e->ctx;
    c.read_file = unit_read_file;
    c.delete_file = unit_delete_file;
    c.fixtures = e->fixtures;
    c.temp = e->temp;
    c.tag = e->tag;
    owf_lib_book_checks(&c);
    pdf = owf_pdf_new();
    if (pdf && owf_pdf_page(pdf, 11906, 16838) == 0) {
        owf_pdf_rect(pdf, 720, 720, 3600, 480, OWF_SHADE(0xDDE4F0), OWF_SHADE(0x808080), 10);
        owf_pdf_text(pdf, 800, 1040, 900, "Calc sheet", 10, OWF_FONT_SANS, OWF_BOLD, 200, 0);
        r = owf_pdf_finish(pdf, "OWFLibsTest Calc", &data, &length, NULL);
    }
    LCHECK(e, r == OWF_OK && data && length > 400 && !memcmp(data, "%PDF-", 5) && owf_pdf_page_count(pdf) == 1,
           "%s: a printed-sheet page from owfcalc.library: %lu bytes", e->tag, (unsigned long)length);
    owf_free(data);
    owf_pdf_free(pdf);
}

OWF_LIBS_UNIT(owf_libs_calc, run);
