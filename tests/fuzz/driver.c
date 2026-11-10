/*
 * Runs a libFuzzer-style harness (LLVMFuzzerTestOneInput) without libFuzzer: each file named on the command line,
 * then, with -m N SEED, N random mutations of each (bit flips, byte runs, cuts, copies), so gcc's
 * AddressSanitizer/UBSan builds can fuzz too. The name of each input goes to stderr first, so a crash names it;
 * -w DIR writes each mutated input there before it runs (to keep the one that crashed).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size);

static unsigned long rng = 1;
static unsigned long rnd(void) { rng = rng * 6364136223846793005UL + 1442695040888963407UL; return rng >> 33; }

static unsigned char *slurp(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb");
    unsigned char *b;
    long len;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); len = ftell(f); fseek(f, 0, SEEK_SET);
    b = malloc(len > 0 ? (size_t)len : 1);
    *n = fread(b, 1, (size_t)len, f);
    fclose(f);
    return b;
}

static size_t mutate(unsigned char *b, size_t n, size_t cap)
{
    int k, times = 1 + (int)(rnd() % 8);
    for (k = 0; k < times && n; k++) {
        size_t at = rnd() % n, len;
        switch (rnd() % 7) {
        case 0: b[at] ^= (unsigned char)(1u << (rnd() % 8)); break;
        case 1: b[at] = (unsigned char)rnd(); break;
        case 2: { static const unsigned char v[] = { 0, 0xff, 0x7f, 0x80, 1, 0xfe };
                  len = 1 + rnd() % 4; while (len-- && at < n) b[at++] = v[rnd() % sizeof v]; } break;
        case 3: n = at + 1; break;                                   /* cut short */
        case 4: len = rnd() % 64; if (n + len <= cap) { memmove(b + at + len, b + at, n - at);
                  memset(b + at, (int)(rnd() & 0xff), len); n += len; } break;
        case 5: { size_t from = rnd() % n; len = rnd() % 32;
                  if (from + len > n) len = n - from;
                  if (at + len > n) len = n - at;
                  memmove(b + at, b + from, len); } break;
        default: { size_t from = rnd() % n; len = rnd() % 4 + 1; if (from + len <= n && at + len <= n) {
                  unsigned char t[4]; memcpy(t, b + at, len); memcpy(b + at, b + from, len); memcpy(b + from, t, len); } }
        }
    }
    return n;
}

int main(int argc, char **argv)
{
    int i = 1, m = 0;
    const char *keep = NULL;
    while (i < argc && argv[i][0] == '-') {
        if (!strcmp(argv[i], "-m") && i + 2 < argc) { m = atoi(argv[i + 1]); rng = strtoul(argv[i + 2], NULL, 10); i += 3; }
        else if (!strcmp(argv[i], "-w") && i + 1 < argc) { keep = argv[i + 1]; i += 2; }
        else break;
    }
    for (; i < argc; i++) {
        size_t n = 0, cap, len;
        unsigned char *seed = slurp(argv[i], &n), *buf;
        int k;
        if (!seed) continue;
        fprintf(stderr, "input %s\n", argv[i]);
        LLVMFuzzerTestOneInput(seed, n);
        cap = n + 4096;
        buf = malloc(cap);
        for (k = 0; k < m; k++) {
            memcpy(buf, seed, n);
            len = mutate(buf, n, cap);
            if (keep) { char p[512]; FILE *f; snprintf(p, sizeof p, "%s/last", keep);
                        if ((f = fopen(p, "wb"))) { fwrite(buf, 1, len, f); fclose(f); } }
            {   /* the case runs from its own exact-size copy, so ASan sees reads past its end */
                unsigned char *c = malloc(len ? len : 1);
                memcpy(c, buf, len);
                LLVMFuzzerTestOneInput(c, len);
                free(c);
            }
        }
        free(buf);
        free(seed);
    }
    fprintf(stderr, "driver done\n");
    return 0;
}
