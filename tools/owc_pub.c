/*
 * OWConvert's Publisher files, on owfpub.library (OWF_USE_PUB_LIBRARY) or
 * with libowf linked in. libowf reads .pub (OpenPublisher's reader) but
 * writes no format a .pub could become yet, so OWConvert says what the file
 * is, and that it can't convert it.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#if defined(OWF_USE_LIBRARY) && !defined(OWF_USE_PUB_LIBRARY)
#error "owc_pub.c is built for owfpub.library (OWF_USE_PUB_LIBRARY)"
#endif
#include "owf.h"
#include "owf_open.h"
#include "owf_pub.h"
#include "owc.h"

#include <stdio.h>

static int opened, missing;

int owc_pub_open(void)
{
    if (!opened && !missing) {
        if (owf_open())
            opened = 1;
        else
            missing = owc_missing(OWF_LIBRARY_NAME, OWF_LIBRARY_VERSION, "Publisher files");
    }
    return opened;
}

void owc_pub_close(void)
{
    if (opened)
        owf_close();
    opened = 0;
}

int owc_pub_convert(const char *from, int show_report)
{
    owf_report *report;
    owf_pub *pub = NULL;
    int result, i;

    if (!owc_pub_open())
        return RC_FAIL;
    report = owf_report_new();
    if (!report) {
        printf("OWConvert: %s\n", owf_error_text(OWF_ERR_MEMORY));
        return RC_FAIL;
    }
    result = owf_pub_read_file(from, &pub, report);
    if (result != OWF_OK) {
        printf("OWConvert: %s: %s\n", from, owf_error_text(result));
    } else {
        printf("%s (%s, %d page(s), %d text(s)): OWConvert can't convert Publisher files yet; OpenPublisher "
               "reads them\n", from, owf_pub_version_name(pub->version), pub->npages, pub->nstories);
        owf_pub_free(pub);
    }
    if (show_report || result != OWF_OK) {
        for (i = 0; i < owf_report_count(report); i++) {
            owf_note_level level;
            int times;
            const char *text = owf_report_note(report, i, &level, &times);
            if (times > 1)
                printf("  %s: %s (%d times)\n", owc_level_name(level), text, times);
            else
                printf("  %s: %s\n", owc_level_name(level), text);
        }
    }
    owf_report_free(report);
    return result == OWF_ERR_MEMORY ? RC_FAIL : RC_ERROR;
}
