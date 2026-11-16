/*
 * pub_resave IN OUT: reads a Publisher file with libowf's reader and writes
 * it again with its writer (out_pub.c), printing the reader's and the
 * writer's notes. For test_pub_save.py, which reads OUT back and compares.
 * Exit 0 written, 10 IN refused, 20 the writer failed.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "owf_pub.h"

#include <stdio.h>
#include <stdlib.h>

static void notes(owf_report *report)
{
    int i;
    for (i = 0; i < owf_report_count(report); i++) {
        owf_note_level level;
        int times;
        const char *note = owf_report_note(report, i, &level, &times);
        printf("note %d %s x%d\n", (int)level, note, times);
    }
}

int main(int argc, char **argv)
{
    owf_report *report = owf_report_new(), *saved = owf_report_new();
    owf_pub *pub = NULL;
    unsigned char *data = NULL;
    size_t length = 0;
    FILE *out;
    int rc;
    if (argc != 3 || !report || !saved) {
        fprintf(stderr, "usage: pub_resave IN OUT\n");
        return 20;
    }
    if (owf_pub_read_file(argv[1], &pub, report) != OWF_OK) {
        notes(report);
        owf_report_free(report);
        owf_report_free(saved);
        return 10;
    }
    rc = owf_pub_write(pub, &data, &length, saved);
    notes(saved);
    owf_pub_free(pub);
    owf_report_free(report);
    owf_report_free(saved);
    if (rc != OWF_OK || !(out = fopen(argv[2], "wb"))) {
        free(data);
        return 20;
    }
    fwrite(data, 1, length, out);
    fclose(out);
    free(data);
    return 0;
}
