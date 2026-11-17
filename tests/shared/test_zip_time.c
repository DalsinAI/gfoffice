/* The time a zip is stamped with (owf_zip.c, owf_sys.c): fixed for tests
 * that save twice and compare the bytes.
 *   - owf_zip_fix_time: every entry's DOS time and date are the fixed one's,
 *     and two zips of the same files are the same bytes;
 *   - OWF_ZIP_EPOCH (the host's owf_sys_zip_epoch): seconds since 1970, UTC;
 *     one that is not a number is the clock's time again;
 *   - owf_zip_fix_time(NULL) gives the clock back;
 *   - the clock itself (owf_sys_now, Calc's TODAY and NOW) stays the real
 *     time while a zip's time is fixed either way.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#define _POSIX_C_SOURCE 200112L
#include "owf_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures, checks;

#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
    printf(__VA_ARGS__); printf("\n"); } } while (0)

/* 14 November 2023, 22:13:20 (OWF_ZIP_EPOCH 1700000000 in UTC) */
#define DOS_TIME ((22u << 11) | (13u << 5) | (20u / 2))
#define DOS_DATE (((2023u - 1980u) << 9) | (11u << 5) | 14u)

/* a zip of two small files; its bytes in *buf */
static int zip_of(owf_buf *buf)
{
    owf_zip *z;
    owf_buf_init(buf);
    z = owf_zip_new(buf);
    if (!z) return 0;
    if (owf_zip_add(z, "mimetype", "application/test", 16, 0) != OWF_OK ||
        owf_zip_add(z, "content.xml", "<a>some text, some text, some text</a>", 38, 1) != OWF_OK) {
        owf_zip_finish(z);
        return 0;
    }
    return owf_zip_finish(z) == OWF_OK;
}

static unsigned le16(const owf_buf *b, size_t at)
{
    const unsigned char *p = (const unsigned char *)b->data + at;
    return (unsigned)(p[0] | (p[1] << 8));
}

/* the first entry's local header: its time at 10, its date at 12 */
static void stamped(const owf_buf *b, unsigned time, unsigned date, const char *what)
{
    CHECK(b->length > 30 && le16(b, 10) == time && le16(b, 12) == date, "%s: time %04x date %04x", what,
          b->length > 30 ? le16(b, 10) : 0, b->length > 30 ? le16(b, 12) : 0);
}

int main(void)
{
    owf_time t = { 2023, 11, 14, 22, 13, 20 };
    owf_buf a, b;

    unsetenv("OWF_ZIP_EPOCH");
    owf_zip_fix_time(&t);
    CHECK(zip_of(&a) && zip_of(&b), "two zips");
    stamped(&a, DOS_TIME, DOS_DATE, "fixed");
    CHECK(a.length == b.length && !memcmp(a.data, b.data, a.length), "fixed: the same bytes twice");
    owf_buf_free(&a);
    owf_buf_free(&b);

    owf_zip_fix_time(NULL);
    setenv("OWF_ZIP_EPOCH", "1700000000", 1);
    CHECK(zip_of(&a), "a zip (OWF_ZIP_EPOCH)");
    stamped(&a, DOS_TIME, DOS_DATE, "OWF_ZIP_EPOCH");
    owf_buf_free(&a);

    /* the clock (TODAY) is still now, both ways of fixing a zip's time on */
    owf_zip_fix_time(&t);
    {
        owf_time now = { 0, 0, 0, 0, 0, 0 };
        CHECK(owf_sys_now(&now) && now.year >= 2026, "the clock with a zip's time fixed: year %d", now.year);
    }
    owf_zip_fix_time(NULL);

    setenv("OWF_ZIP_EPOCH", "soon", 1);
    CHECK(zip_of(&a), "a zip (OWF_ZIP_EPOCH not a number)");
    CHECK(a.length > 30 && (le16(&a, 12) >> 9) + 1980 >= 2026, "not a number: the clock's year (%u)",
          a.length > 30 ? (le16(&a, 12) >> 9) + 1980 : 0);
    owf_buf_free(&a);

    unsetenv("OWF_ZIP_EPOCH");
    CHECK(zip_of(&a), "a zip (the clock)");
    CHECK(a.length > 30 && (le16(&a, 12) >> 9) + 1980 >= 2026, "the clock's year (%u)",
          a.length > 30 ? (le16(&a, 12) >> 9) + 1980 : 0);
    owf_buf_free(&a);

    printf("test_zip_time: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
