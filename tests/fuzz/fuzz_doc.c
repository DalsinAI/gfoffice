/*
 * libFuzzer harness: libowf's document filters on hostile bytes (format found from the contents), then the
 * document written out in every writable format, as GFOpenWrite would save it.
 *   clang -g -O1 -fsanitize=fuzzer,address,undefined -Ilibowf/include -Ilibowf/src libowf/src/[a-z]*.c \
 *         tests/fuzz/fuzz_doc.c -o build/fuzz/fuzz_doc
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "owf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size)
{
    owf_doc *doc = NULL;
    owf_report *report = owf_report_new();
    const owf_format *used = NULL;
    int i;

    if (owf_import_memory(data, size, NULL, &doc, report, &used) == OWF_OK && doc) {
        for (i = 0; i < owf_format_count(); i++) {
            const owf_format *f = owf_format_at(i);
            unsigned char *out = NULL;
            size_t length = 0;
            if (f && f->export && f->export(doc, &out, &length, report) == OWF_OK)
                owf_free(out);
        }
    }
    owf_doc_free(doc);
    owf_report_free(report);
    return 0;
}
