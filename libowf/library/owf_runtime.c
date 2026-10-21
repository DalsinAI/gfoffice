/*
 * owf.library's C library: the calls libowf and the compiler use, so the
 * library links with no C library (-nostdlib) and never depends on a
 * caller's. The string calls are weblayout's (DalsinAI/openamigabrowser
 * src/weblayout/library/wl_runtime.c); printf, strtol and the case calls
 * are owf_libc.c's.
 *
 * Memory: one Exec pool for the library, behind a semaphore, with each
 * block's size in front of it. Every document's memory comes from it, and
 * programs give it back through owf_free() (OpenWrite's editor grows a
 * document's arrays itself), so any task may free what another allocated.
 *
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/semaphores.h>
#include <proto/exec.h>

#include <stdarg.h>
#include <stddef.h>

#include "owf_libc.h"
#include "owf_runtime.h"

/* ---- memory -------------------------------------------------------------- */

static APTR pool;
static struct SignalSemaphore pool_lock;

/* in front of every block: what the caller asked for, kept to 8 bytes so
 * the block stays aligned for any type */
struct block {
    ULONG size;
    ULONG pad;
};

#define MAX_BLOCK 0x7fff0000UL

int owf_rt_init(void)
{
    InitSemaphore(&pool_lock);
    /* 16K puddles; blocks of 4K and more get memory of their own, which
     * FreePooled gives straight back to the system */
    pool = CreatePool(MEMF_ANY, 16384, 4096);
    return pool != NULL;
}

void owf_rt_cleanup(void)
{
    if (pool) {
        DeletePool(pool);
        pool = NULL;
    }
}

void *malloc(size_t n)
{
    struct block *b;
    ULONG bytes;
    if (n > MAX_BLOCK) return NULL;
    bytes = (ULONG)n + sizeof *b;
    ObtainSemaphore(&pool_lock);
    b = (struct block *)AllocPooled(pool, bytes);
    ReleaseSemaphore(&pool_lock);
    if (!b) return NULL;
    b->size = (ULONG)n;
    return b + 1;
}

void free(void *p)
{
    struct block *b;
    if (!p) return;
    b = (struct block *)p - 1;
    ObtainSemaphore(&pool_lock);
    FreePooled(pool, b, b->size + sizeof *b);
    ReleaseSemaphore(&pool_lock);
}

void *calloc(size_t n, size_t size)
{
    size_t bytes;
    unsigned char *p;
    if (size && n > MAX_BLOCK / size) return NULL;
    bytes = n * size;
    p = (unsigned char *)malloc(bytes);
    if (p) {
        size_t i;
        for (i = 0; i < bytes; i++) p[i] = 0;
    }
    return p;
}

void *realloc(void *p, size_t n)
{
    struct block *b;
    unsigned char *q;
    size_t i, keep;
    if (!p) return malloc(n);
    if (!n) { free(p); return NULL; }
    b = (struct block *)p - 1;
    if (n <= b->size && n >= b->size / 2) return p;  /* close enough: keep it */
    q = (unsigned char *)malloc(n);
    if (!q) return NULL;
    keep = n < b->size ? n : b->size;
    for (i = 0; i < keep; i++) q[i] = ((unsigned char *)p)[i];
    free(p);
    return q;
}

/* ---- strings and memory -------------------------------------------------- */

void *memcpy(void *dst, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (n--) *d++ = *s++;
    return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    if (d == s || !n) return dst;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--) *--d = *--s;
    }
    return dst;
}

void *memset(void *dst, int c, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    while (n--) *d++ = (unsigned char)c;
    return dst;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *p = (const unsigned char *)a, *q = (const unsigned char *)b;
    for (; n; n--, p++, q++)
        if (*p != *q) return (int)*p - (int)*q;
    return 0;
}

void *memchr(const void *s, int c, size_t n)
{
    const unsigned char *p = (const unsigned char *)s;
    for (; n; n--, p++)
        if (*p == (unsigned char)c) return (void *)p;
    return NULL;
}

size_t strlen(const char *s)
{
    const char *p = s;
    while (*p) p++;
    return (size_t)(p - s);
}

int strcmp(const char *a, const char *b)
{
    unsigned char ca, cb;
    do {
        ca = (unsigned char)*a++;
        cb = (unsigned char)*b++;
    } while (ca == cb && ca);
    return (int)ca - (int)cb;
}

int strncmp(const char *a, const char *b, size_t n)
{
    unsigned char ca = 0, cb = 0;
    while (n--) {
        ca = (unsigned char)*a++;
        cb = (unsigned char)*b++;
        if (ca != cb || !ca) break;
    }
    return (int)ca - (int)cb;
}

char *strchr(const char *s, int c)
{
    for (;; s++) {
        if (*s == (char)c) return (char *)s;
        if (!*s) return NULL;
    }
}

char *strrchr(const char *s, int c)
{
    const char *last = NULL;
    for (;; s++) {
        if (*s == (char)c) last = s;
        if (!*s) return (char *)last;
    }
}

char *strstr(const char *hay, const char *needle)
{
    size_t n = strlen(needle);
    if (!n) return (char *)hay;
    for (; *hay; hay++)
        if (*hay == *needle && !strncmp(hay, needle, n)) return (char *)hay;
    return NULL;
}

size_t strcspn(const char *s, const char *reject)
{
    const char *p = s;
    for (; *p; p++)
        if (strchr(reject, *p)) break;
    return (size_t)(p - s);
}

char *strcpy(char *dst, const char *src)
{
    char *d = dst;
    while ((*d++ = *src++) != 0)
        ;
    return dst;
}

char *strncpy(char *dst, const char *src, size_t n)
{
    char *d = dst;
    while (n && *src) { *d++ = *src++; n--; }
    while (n--) *d++ = 0;
    return dst;
}

int strcasecmp(const char *a, const char *b)
{
    return owf_rt_strcasecmp(a, b);
}

int strncasecmp(const char *a, const char *b, size_t n)
{
    return owf_rt_strncasecmp(a, b, n);
}

long strtol(const char *s, char **end, int base)
{
    return owf_rt_strtol(s, end, base);
}

unsigned long strtoul(const char *s, char **end, int base)
{
    return owf_rt_strtoul(s, end, base);
}

int abs(int v)
{
    return v < 0 ? -v : v;
}

/* ---- printf -------------------------------------------------------------- */

int vsnprintf(char *out, size_t size, const char *fmt, va_list ap)
{
    return owf_rt_vsnprintf(out, size, fmt, ap);
}

int snprintf(char *out, size_t size, const char *fmt, ...)
{
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = owf_rt_vsnprintf(out, size, fmt, ap);
    va_end(ap);
    return n;
}
