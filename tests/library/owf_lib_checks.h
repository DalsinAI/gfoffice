/*
 * owf.library 2's checks (owf_lib_checks.c): workbooks, the formula
 * engine, file kinds, zip and inflate, and the .pub reader, in plain C on
 * libowf's calls. OWFLibTest runs them through LIBS:owf.library, in two
 * tasks at once; test_owf_lib_checks.c runs the same ones on the PC with
 * libowf linked in and the library's own maths (make libtest-host), so
 * their expected answers are proven before an Amiga runs them.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#ifndef OWF_LIB_CHECKS_H
#define OWF_LIB_CHECKS_H

#include <stddef.h>

typedef struct {
    void (*check)(void *ctx, int ok, const char *what);
    void *ctx;
    /* a whole file into memory from owf_malloc; 0 when it can't */
    int (*read_file)(const char *path, unsigned char **data, size_t *length);
    void (*delete_file)(const char *path);
    const char *fixtures;        /* book.xls, book-lo.xlsx, book-lo.ods, Sample.pub: "PROGDIR:fixtures" */
    const char *temp;            /* put before a file name: "T:", "build/host/" */
    const char *tag;             /* in the checks' names and the temporary files' */
} owf_lib_checks;

void owf_lib_run_checks(const owf_lib_checks *c);
/* OWFLibsTest's parts of them (owf_lib_checks.c built with OWF_LIB_CHECKS_PART
 * 1 and 3): the workbooks and the engine, through owfcalc.library; the .pub
 * reader, through owfpub.library */
void owf_lib_book_checks(const owf_lib_checks *c);
void owf_lib_pub_checks(const owf_lib_checks *c);

#endif
