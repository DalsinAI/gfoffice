/*
 * owf.library's own printf, strtol, strtoul and case calls
 * (libowf/library/owf_libc.c) against the C library's, on the PC, for every
 * format libowf writes and the numbers its readers parse.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "owf_libc.h"

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static int failures, checks;

static int ours(char *out, size_t size, const char *fmt, ...)
{
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = owf_rt_vsnprintf(out, size, fmt, ap);
    va_end(ap);
    return n;
}

#define SAME(size, fmt, ...) do { \
    char a[256], b[256]; \
    int na, nb; \
    memset(a, 'x', sizeof a); memset(b, 'x', sizeof b); \
    na = ours(a, size, fmt, __VA_ARGS__); \
    nb = snprintf(b, size, fmt, __VA_ARGS__); \
    checks++; \
    if (na != nb || memcmp(a, b, size ? size : 1) != 0) { \
        failures++; \
        printf("FAIL %s: ours %d \"%.*s\", libc %d \"%.*s\"\n", fmt, na, (int)(size ? size : 1), a, nb, \
               (int)(size ? size : 1), b); \
    } \
} while (0)

static void numbers(const char *s, int base)
{
    char *ea, *eb;
    long la = owf_rt_strtol(s, &ea, base), lb = strtol(s, &eb, base);
    unsigned long ua, ub;
    checks++;
    if (la != lb || ea != eb) {
        failures++;
        printf("FAIL strtol(\"%s\", %d): ours %ld +%d, libc %ld +%d\n", s, base, la, (int)(ea - s), lb, (int)(eb - s));
    }
    ua = owf_rt_strtoul(s, &ea, base);
    ub = strtoul(s, &eb, base);
    checks++;
    if (ua != ub || ea != eb) {
        failures++;
        printf("FAIL strtoul(\"%s\", %d): ours %lu +%d, libc %lu +%d\n", s, base, ua, (int)(ea - s), ub, (int)(eb - s));
    }
}

int main(void)
{
    static const char *const nums[] = {
        "0", "12", "-12", "+7", "  42xyz", "0x1F", "1f", "FFFFFF", "00ff80", "010", "",
        "x", "-", "2147483647", "2147483648", "-2147483648", "-2147483649",
        "4294967295", "4294967296", "99999999999999999999", "-1", "0x", "7fffffff"
    };
    static const int bases[] = { 0, 10, 16, 8 };
    const char nonul[4] = { 'a', 'b', 'c', 'd' };
    size_t i, j;

    /* what libowf writes */
    SAME(256, "%s", "text");
    SAME(256, "%s%s", "word/", "document.xml");
    SAME(256, "%.*s", 3, "abcdef");
    SAME(256, "%.*s", 4, nonul);
    SAME(256, "%.63s_rels/%.200s.rels", "word/", "document.xml");
    SAME(256, "%06lX", 0xA0B0CUL);
    SAME(256, "%06lx", 0xffUL);
    SAME(256, "%010lu", 1234UL);
    SAME(256, "%03o", 7);
    SAME(256, "\\%03o", 200);
    SAME(256, "%03u", 5u);
    SAME(256, "%d", 0);
    SAME(256, "%d", -2147483647 - 1);
    SAME(256, "%d", 2147483647);
    SAME(256, "%lu", 4294967295UL);
    SAME(256, "%u", 65535u);
    SAME(256, "%%%d.", 3);
    SAME(256, "Heading%d", 6);
    SAME(256, "%c%c", 'O', 'K');
    SAME(256, "%-5s|", "ab");
    SAME(256, "%5s|", "ab");
    SAME(256, "%5d|%-5d|%05d", 42, 42, -42);
    SAME(256, "%x %X", 255u, 255u);
    SAME(256, "%ld %li", -5L, 6L);
    /* cut short, and a size of nothing */
    SAME(4, "%s", "abcdef");
    SAME(1, "%d", 12345);
    SAME(0, "%d", 12345);

    for (i = 0; i < sizeof nums / sizeof nums[0]; i++)
        for (j = 0; j < sizeof bases / sizeof bases[0]; j++)
            numbers(nums[i], bases[j]);

    checks += 4;
    if ((owf_rt_strcasecmp("Word.DOCX", "word.docx") != 0) != (strcasecmp("Word.DOCX", "word.docx") != 0)) failures++;
    if ((owf_rt_strcasecmp(".png", ".jpg") > 0) != (strcasecmp(".png", ".jpg") > 0)) failures++;
    if ((owf_rt_strncasecmp("odtXX", "ODTyy", 3) == 0) != (strncasecmp("odtXX", "ODTyy", 3) == 0)) failures++;
    if ((owf_rt_strcasecmp("a", "ab") < 0) != (strcasecmp("a", "ab") < 0)) failures++;

    if (failures) {
        printf("owf.library libc tests: %d of %d failed\n", failures, checks);
        return 1;
    }
    printf("owf.library libc tests passed (%d checks)\n", checks);
    return 0;
}
