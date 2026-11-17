/*
 * OWFLibsTest's owf_libs_diag.c on the PC (OWF_LIBS_HOST): libowf and the editing core linked in, owf_kind.c built
 * as owfdiag.library's (OWF_KIND_DIAGRAMS), the checks printed as test lines and a main() that runs them; the
 * shared calls' checks are OWFLibsTest's own on the Amiga (owf_libs_shared.h), so they are left out here.
 *   test-owf-libs-diag [TEMP]     (default /tmp/)
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef OWF_LIBS_HOST_H
#define OWF_LIBS_HOST_H

#include <stdio.h>
#include <string.h>

typedef struct {
    const char *temp;            /* put before a temporary file's name */
    const char *tag;
} owf_libs_env;

static int host_passed, host_failed;

#define LCHECK(env, ok, ...) do { \
    char what_[160]; \
    int ok_ = (ok); \
    (void)(env); \
    snprintf(what_, sizeof what_, __VA_ARGS__); \
    printf("%s %s\n", ok_ ? "ok  " : "FAIL", what_); \
    if (ok_) host_passed++; else host_failed++; \
} while (0)

static void unit_delete_file(const char *path)
{
    remove(path);
}

static void shared_checks(const owf_libs_env *e, owf_kind text_kind)
{
    (void)e;
    (void)text_kind;
}

#define OWF_LIBS_UNIT(var, run) \
    int main(int argc, char **argv) \
    { \
        owf_libs_env env; \
        env.temp = argc > 1 ? argv[1] : "/tmp/"; \
        env.tag = "host diag"; \
        run(&env); \
        printf("test-owf-libs-diag: %d passed, %d failed\n", host_passed, host_failed); \
        return host_failed != 0; \
    }

#endif
