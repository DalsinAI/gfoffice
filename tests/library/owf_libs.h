/*
 * OWFLibsTest (owf_libs_test.c): GFOffice's four libraries from LIBS:, as
 * programs use them (docs/suite/owf-libraries.md). Each library's calls are
 * in a file of their own, built for that library (owf_proto.h: one
 * library's calls per file), which gives the test one of these.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#ifndef OWF_LIBS_H
#define OWF_LIBS_H

#include <stddef.h>

typedef struct {
    void (*check)(void *ctx, int ok, const char *what);
    void *ctx;
    const char *samples;         /* OpenWrite-Tour.docx and .odt: "PROGDIR:samples" */
    const char *fixtures;        /* book.xls, book-lo.xlsx, book-lo.ods, Sample.pub, libreoffice.odp */
    const char *temp;            /* put before a temporary file's name: "T:" */
    const char *tag;             /* in the checks' names and the temporary files' */
} owf_libs_env;

typedef struct {
    const char *name;            /* "owfwrite.library" */
    int version;                 /* the version the test opens */
    int (*open)(void);           /* owf_open() (owf_open.h): 1, or 0 */
    void (*close)(void);
    const char *(*why)(void);    /* why it didn't open */
    void (*opened)(char *out, size_t size);   /* "owfwrite.library 1.0 (25.10.2026) libowf ..." */
    void (*run)(const owf_libs_env *env);     /* its checks: the shared calls', then its own */
} owf_libs_unit;

extern const owf_libs_unit owf_libs_write, owf_libs_calc, owf_libs_pub, owf_libs_present;

#endif
