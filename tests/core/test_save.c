/*
 * Saving (libowf/src/owf_sys.c, owf_sys_write_file, under every Save): a
 * save that fails leaves the file as it was (the review of 1 November 2026:
 * the file was cut to nothing first, so a full disk lost the document). The
 * writes fail here as on a full disk, through a file size limit
 * (RLIMIT_FSIZE): the old file must come through unchanged, with nothing
 * left beside it. A save that works replaces the file and leaves nothing
 * else; a drawer or a file that can't be written over is not replaced.
 * Runs in a new drawer under /tmp (or TMPDIR), removed at the end.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#define _POSIX_C_SOURCE 200809L
#include "owf_internal.h"

#include <dirent.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures, checks;
static char dir[512];

#define CHECK(x) do { ++checks; if (!(x)) { \
    printf("FAIL line=%d expr=%s\n", __LINE__, #x); ++failures; \
} } while (0)

static void in_dir(char *out, size_t size, const char *name)
{
    snprintf(out, size, "%s/%s", dir, name);
}

/* 1 when the file holds exactly these bytes. */
static int holds(const char *path, const unsigned char *data, size_t length)
{
    unsigned char *got;
    size_t n;
    int same;
    if (owf_sys_read_file(path, &got, &n) != OWF_OK)
        return 0;
    same = n == length && (!length || memcmp(got, data, length) == 0);
    free(got);
    return same;
}

/* How many names the drawer holds (. and .. not counted). */
static int entries(void)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    int n = 0;
    if (!d)
        return -1;
    while ((e = readdir(d)))
        if (strcmp(e->d_name, ".") && strcmp(e->d_name, ".."))
            n++;
    closedir(d);
    return n;
}

/* Writes past limit bytes fail (EFBIG) from here on; 0 lifts it again. */
static void limit_writes(rlim_t limit)
{
    struct rlimit r;
    getrlimit(RLIMIT_FSIZE, &r);
    r.rlim_cur = limit ? limit : r.rlim_max;
    setrlimit(RLIMIT_FSIZE, &r);
}

static unsigned char *big(size_t n, int seed)
{
    unsigned char *p = malloc(n);
    size_t i;
    for (i = 0; p && i < n; i++)
        p[i] = (unsigned char)(i * 7 + seed);
    return p;
}

static void test_new_and_replaced(void)
{
    static const unsigned char one[] = "the first version";
    static const unsigned char two[] = "the second, longer version of it";
    char path[600];
    in_dir(path, sizeof path, "Letter.odt");
    CHECK(owf_sys_write_file(path, one, sizeof one - 1) == OWF_OK);
    CHECK(holds(path, one, sizeof one - 1));
    CHECK(entries() == 1);
    CHECK(owf_sys_write_file(path, two, sizeof two - 1) == OWF_OK);
    CHECK(holds(path, two, sizeof two - 1));
    CHECK(owf_sys_write_file(path, one, 0) == OWF_OK);      /* an empty file is a file */
    CHECK(holds(path, one, 0));
    CHECK(entries() == 1);
    remove(path);
}

/* The review's case: the disk fills part way through a save. */
static void test_failed_write_keeps_the_file(void)
{
    size_t n = 256 * 1024;
    unsigned char *old = big(n, 1), *new_ = big(n, 2);
    char path[600];
    in_dir(path, sizeof path, "Report.odt");
    CHECK(old && new_);
    if (!old || !new_) { free(old); free(new_); return; }
    CHECK(owf_sys_write_file(path, old, n) == OWF_OK);
    limit_writes(n / 2);                        /* the disk fills half way through */
    CHECK(owf_sys_write_file(path, new_, n) == OWF_ERR_IO);
    limit_writes(0);
    CHECK(holds(path, old, n));
    CHECK(entries() == 1);                      /* no temporary or .bak file left */
    /* a new file that can't be written leaves nothing */
    in_dir(path, sizeof path, "New.odt");
    limit_writes(4096);
    CHECK(owf_sys_write_file(path, new_, n) == OWF_ERR_IO);
    limit_writes(0);
    CHECK(entries() == 1);
    remove(path);
    in_dir(path, sizeof path, "Report.odt");
    remove(path);
    free(old);
    free(new_);
}

/* What couldn't be written over before isn't replaced now either. */
static void test_not_replaced(void)
{
    static const unsigned char text[] = "kept";
    char path[600], inner[700];
    in_dir(path, sizeof path, "Drawer");
    mkdir(path, 0755);
    CHECK(owf_sys_write_file(path, text, 4) == OWF_ERR_IO);
    snprintf(inner, sizeof inner, "%s/inside", path);
    CHECK(owf_sys_write_file(inner, text, 4) == OWF_OK);    /* a drawer in the path: the name's in it */
    CHECK(holds(inner, text, 4));
    remove(inner);
    rmdir(path);
    in_dir(path, sizeof path, "ReadOnly.txt");
    CHECK(owf_sys_write_file(path, text, 4) == OWF_OK);
    chmod(path, 0444);
    if (access(path, W_OK) != 0) {              /* (as root it can be written anyway) */
        CHECK(owf_sys_write_file(path, (const unsigned char *)"gone", 4) == OWF_ERR_IO);
        CHECK(holds(path, text, 4));
    }
    chmod(path, 0644);
    remove(path);
    CHECK(entries() == 0);
}

/* A bare name: the current drawer. */
static void test_bare_name(void)
{
    static const unsigned char text[] = "here";
    char was[512];
    if (!getcwd(was, sizeof was) || chdir(dir) != 0) {
        CHECK(0);
        return;
    }
    CHECK(owf_sys_write_file("Bare.txt", text, 4) == OWF_OK);
    CHECK(owf_sys_write_file("Bare.txt", text, 3) == OWF_OK);
    CHECK(holds("Bare.txt", text, 3));
    CHECK(entries() == 1);
    remove("Bare.txt");
    if (chdir(was) != 0)
        CHECK(0);
}

int main(void)
{
    const char *tmp = getenv("TMPDIR");
    signal(SIGXFSZ, SIG_IGN);                   /* a write past the limit fails instead of stopping us */
    snprintf(dir, sizeof dir, "%s/owf-save-XXXXXX", tmp && *tmp ? tmp : "/tmp");
    if (!mkdtemp(dir)) {
        printf("test-save: no drawer to work in\n");
        return 1;
    }
    test_new_and_replaced();
    test_failed_write_keeps_the_file();
    test_not_replaced();
    test_bare_name();
    rmdir(dir);
    printf("test-save: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
