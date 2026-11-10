/*
 * libFuzzer harness: libowf's workbook filters (GFOpenCalc's) on hostile bytes, then the workbook written out in
 * every writable workbook format, as GFOpenCalc's SAVE and EXPORT would.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "owf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size)
{
    owf_book *book = NULL;
    owf_report *report = owf_report_new();
    const owf_book_format *used = NULL;
    int i;

    if (owf_import_book_memory(data, size, NULL, &book, report, &used) == OWF_OK && book) {
        for (i = 0; i < owf_book_format_count(); i++) {
            const owf_book_format *f = owf_book_format_at(i);
            unsigned char *out = NULL;
            size_t length = 0;
            if (f && f->export && f->export(book, &out, &length, report) == OWF_OK)
                owf_free(out);
        }
    }
    owf_book_free(book);
    owf_report_free(report);
    return 0;
}
