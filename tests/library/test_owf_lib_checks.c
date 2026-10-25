/*
 * OWFLibTest's owf.library 2 checks (owf_lib_checks.c) on the PC, with
 * libowf linked in: make libtest-host builds it with the library's own
 * maths (owf_libm.c through libm_shim.c, no -lm), as the Amiga's library
 * has them.
 *
 *   test-owf-lib-checks FIXTURES_DIR TEMP_PREFIX
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "owf.h"
#include "owf_lib_checks.h"

#include <stdio.h>
#include <stdlib.h>

static int passed, failed;

static void check(void *ctx, int ok, const char *what)
{
    (void)ctx;
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (ok)
        passed++;
    else
        failed++;
}

static int read_file(const char *path, unsigned char **data, size_t *length)
{
    FILE *f = fopen(path, "rb");
    long n;
    *data = NULL;
    *length = 0;
    if (!f)
        return 0;
    if (fseek(f, 0, SEEK_END) || (n = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) ||
        !(*data = owf_malloc((size_t)n + 1)) || fread(*data, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        owf_free(*data);
        *data = NULL;
        return 0;
    }
    fclose(f);
    *length = (size_t)n;
    return 1;
}

static void delete_file(const char *path)
{
    remove(path);
}

int main(int argc, char **argv)
{
    owf_lib_checks c;
    if (argc != 3) {
        fprintf(stderr, "test-owf-lib-checks FIXTURES_DIR TEMP_PREFIX\n");
        return 2;
    }
    c.check = check;
    c.ctx = NULL;
    c.read_file = read_file;
    c.delete_file = delete_file;
    c.fixtures = argv[1];
    c.temp = argv[2];
    c.tag = "pc";
    owf_lib_run_checks(&c);
    printf("test-owf-lib-checks: %d passed, %d failed\n", passed, failed);
    return failed != 0;
}
