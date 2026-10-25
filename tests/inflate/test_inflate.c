/*
 * libowf's own inflate (libowf/src/owf_inflate.c), on the PC:
 *
 *   test-inflate                 all the checks below
 *   test-inflate --bench ZIP...  each zip's deflated entries inflated by
 *                                zlib and by ours, and the times
 *
 * 1. zlib's own raw deflate output, at every level and strategy, of random
 *    and structured data, read back byte for byte;
 * 2. hand-made streams for the edges: empty blocks, a stored block of
 *    65,535 bytes, a distance of 32,768, a length of 258, dynamic blocks
 *    with a single code or none, and every rule broken once;
 * 3. damaged streams (bits flipped, bytes changed, cut short, random),
 *    whose verdict and bytes must be zlib's. Run under ASan and UBSan by
 *    make check, with buffers of exactly the stated sizes.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#define _POSIX_C_SOURCE 199309L   /* clock_gettime */

#include "owf.h"
#include "owf_inflate.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <zlib.h>

static int failures, checks;

#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
    printf(__VA_ARGS__); printf("\n"); } } while (0)

/* ---- a small PRNG (xorshift32), so every run tries the same cases ---- */

static unsigned long rng = 2463534242UL;

static unsigned long next_random(void)
{
    rng ^= (rng << 13) & 0xFFFFFFFFUL;
    rng ^= rng >> 17;
    rng ^= (rng << 5) & 0xFFFFFFFFUL;
    return rng & 0xFFFFFFFFUL;
}

static size_t below(size_t n)
{
    return n ? (size_t)(next_random() % n) : 0;
}

/* ---- inflating, ours and zlib's, with buffers of exactly the sizes given ---- */

/* ours; *out from malloc (exactly out_length bytes, at least 1) */
static int ours(const unsigned char *in, size_t in_length, size_t out_length, unsigned char **out)
{
    unsigned char *copy = malloc(in_length ? in_length : 1), *o = malloc(out_length ? out_length : 1);
    int r;
    if (!copy || !o) {
        printf("out of memory\n");
        exit(2);
    }
    if (in_length)
        memcpy(copy, in, in_length);
    r = owf_inflate(o, out_length, copy, in_length);
    free(copy);
    *out = o;
    return r;
}

/* zlib's verdict as owf_unzip.c takes it: Z_STREAM_END with exactly out_length bytes */
static int theirs(const unsigned char *in, size_t in_length, size_t out_length, unsigned char **out)
{
    z_stream z;
    unsigned char *o = malloc(out_length ? out_length : 1);
    int r;
    memset(&z, 0, sizeof z);
    if (!o || inflateInit2(&z, -15) != Z_OK) {
        printf("zlib inflateInit2 failed\n");
        exit(2);
    }
    z.next_in = (Bytef *)in;
    z.avail_in = (uInt)in_length;
    z.next_out = o;
    z.avail_out = (uInt)out_length;
    r = inflate(&z, Z_FINISH);
    inflateEnd(&z);
    *out = o;
    return r == Z_STREAM_END && z.total_out == out_length ? OWF_OK : OWF_ERR_CORRUPT;
}

/* zlib's raw deflate of data; flush_every > 0 adds a flush (and so an empty
 * stored block, or a full flush's) that often. */
static unsigned char *zdeflate(const unsigned char *data, size_t length, int level, int bits, int mem, int strategy,
                               size_t flush_every, int flush, size_t *packed)
{
    z_stream z;
    size_t cap = length + length / 8 + 1024 + (flush_every ? (length / flush_every + 1) * 16 : 0), done = 0;
    unsigned char *out = malloc(cap);
    int r;
    memset(&z, 0, sizeof z);
    if (!out || deflateInit2(&z, level, Z_DEFLATED, -bits, mem, strategy) != Z_OK) {
        printf("zlib deflateInit2 failed\n");
        exit(2);
    }
    z.next_out = out;
    z.avail_out = (uInt)cap;
    do {
        size_t n = flush_every && length - done > flush_every ? flush_every : length - done;
        z.next_in = (Bytef *)data + done;
        z.avail_in = (uInt)n;
        done += n;
        r = deflate(&z, done == length ? Z_FINISH : flush);
    } while (done < length && r == Z_OK);
    if (r != Z_STREAM_END) {
        printf("zlib deflate failed (%d)\n", r);
        exit(2);
    }
    *packed = z.total_out;
    deflateEnd(&z);
    return out;
}

/* ---- data to compress ---- */

enum { RANDOM, TEXT, XML, RUNS, PERIODIC, SMALL_ALPHABET, MIXED, NKINDS };
static const char *kind_names[NKINDS] = { "random", "text", "xml", "runs", "periodic", "small alphabet", "mixed" };

static void make_data(int kind, unsigned char *d, size_t n)
{
    static const char *words[] = { "the ", "Amiga ", "document ", "OpenWrite ", "and ", "of ", "workbench ",
                                   "68040 ", "chip ", "RAM ", "\n", "paragraph. ", "z.library ", "a " };
    static const char *tags[] = { "<text:p text:style-name=\"P1\">", "</text:p>", "<w:r><w:t>", "</w:t></w:r>",
                                  "<table:table-cell office:value-type=\"float\" office:value=\"", "\"/>", "Hello ",
                                  "<style:style style:name=\"T", "\" style:family=\"text\">" };
    size_t i = 0, period = 1 + below(32768);
    while (i < n) {
        int k = kind == MIXED ? (int)below(MIXED) : kind;
        size_t run = kind == MIXED ? 1 + below(4000) : n;
        for (; run && i < n; run--) {
            switch (k) {
            case RANDOM:
                d[i++] = (unsigned char)next_random();
                break;
            case TEXT:
            case XML: {
                const char *w = k == TEXT ? words[below(sizeof words / sizeof *words)]
                                          : tags[below(sizeof tags / sizeof *tags)];
                for (; *w && i < n; w++)
                    d[i++] = (unsigned char)*w;
                if (k == XML && i < n && below(3) == 0)
                    d[i++] = (unsigned char)('0' + below(10));
                break;
            }
            case RUNS: {
                size_t r = 1 + below(below(8) == 0 ? 2000 : 20);
                unsigned char c = (unsigned char)below(4);
                for (; r && i < n; r--)
                    d[i++] = c;
                break;
            }
            case PERIODIC:
                d[i] = i < period ? (unsigned char)next_random() : d[i - period];
                if (below(5000) == 0)
                    d[i] ^= 1;
                i++;
                break;
            default:
                d[i++] = (unsigned char)("ACGT"[below(4)]);
                break;
            }
        }
    }
}

/* ---- 1. zlib's deflate, read back ---- */

static int round_trip(const char *what, const unsigned char *data, size_t length, int level, int bits, int mem,
                      int strategy, size_t flush_every, int flush)
{
    size_t packed;
    unsigned char *z = zdeflate(data, length, level, bits, mem, strategy, flush_every, flush, &packed), *o;
    int r = ours(z, packed, length, &o), ok = r == OWF_OK && (length == 0 || memcmp(o, data, length) == 0);
    CHECK(ok, "%s: %lu bytes, level %d, window %d, mem %d, strategy %d, flush %lu/%d: result %d", what,
          (unsigned long)length, level, bits, mem, strategy, (unsigned long)flush_every, flush, r);
    free(o);
    if (length) {   /* one byte more or less than the directory says: refused */
        r = ours(z, packed, length - 1, &o);
        CHECK(r == OWF_ERR_CORRUPT, "%s: %lu bytes into one fewer: %d", what, (unsigned long)length, r);
        free(o);
    }
    r = ours(z, packed, length + 1, &o);
    CHECK(r == OWF_ERR_CORRUPT, "%s: %lu bytes into one more: %d", what, (unsigned long)length, r);
    free(o);
    free(z);
    return ok;
}

static void test_zlib(void)
{
    static const size_t sizes[] = { 0, 1, 2, 3, 100, 258, 259, 4095, 32768, 32769, 65535, 65536, 100000 };
    static const int strategies[] = { Z_DEFAULT_STRATEGY, Z_FILTERED, Z_HUFFMAN_ONLY, Z_RLE, Z_FIXED };
    unsigned char *data = malloc(1 << 20);
    size_t s;
    int kind, level, st, n = 0;

    for (kind = 0; kind < NKINDS; kind++)
        for (s = 0; s < sizeof sizes / sizeof *sizes; s++) {
            make_data(kind, data, sizes[s]);
            for (level = 0; level <= 9; level++)
                n += round_trip(kind_names[kind], data, sizes[s], level, 15, 8, Z_DEFAULT_STRATEGY, 0, Z_NO_FLUSH);
            for (st = 1; st < 5; st++)
                n += round_trip(kind_names[kind], data, sizes[s], 6, 15, 8, strategies[st], 0, Z_NO_FLUSH);
        }
    /* every window size and memory level; flushes (empty stored blocks); a megabyte */
    for (kind = 0; kind < NKINDS; kind++) {
        make_data(kind, data, 100000);
        for (s = 9; s <= 15; s++)
            n += round_trip(kind_names[kind], data, 100000, 6, (int)s, 1 + (int)(s - 9) % 9, Z_DEFAULT_STRATEGY, 0,
                            Z_NO_FLUSH);
        n += round_trip(kind_names[kind], data, 100000, 6, 15, 9, Z_DEFAULT_STRATEGY, 777, Z_SYNC_FLUSH);
        n += round_trip(kind_names[kind], data, 100000, 1, 15, 8, Z_DEFAULT_STRATEGY, 30000, Z_FULL_FLUSH);
        n += round_trip(kind_names[kind], data, 100000, 6, 15, 8, Z_FIXED, 5000, Z_SYNC_FLUSH);
        make_data(kind, data, 1 << 20);
        n += round_trip(kind_names[kind], data, 1 << 20, 6, 15, 8, Z_DEFAULT_STRATEGY, 0, Z_NO_FLUSH);
    }
    printf("%d zlib streams read back\n", n);
    free(data);
}

/* ---- 2. hand-made streams ---- */

typedef struct {
    unsigned char data[140000];
    size_t length;
    unsigned long acc;
    int n;
} bitwriter;

static void bw_init(bitwriter *w)
{
    w->length = 0;
    w->acc = 0;
    w->n = 0;
}

/* value, first bit lowest (RFC 1951's numbers and extra bits) */
static void put(bitwriter *w, unsigned long value, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        w->acc |= ((value >> i) & 1) << w->n;
        if (++w->n == 8) {
            w->data[w->length++] = (unsigned char)w->acc;
            w->acc = 0;
            w->n = 0;
        }
    }
}

/* a Huffman code, first bit highest */
static void put_code(bitwriter *w, unsigned code, int len)
{
    int i;
    for (i = len - 1; i >= 0; i--)
        put(w, (code >> i) & 1, 1);
}

static void bw_flush(bitwriter *w)
{
    if (w->n)
        put(w, 0, 8 - w->n);
}

static void bw_bytes(bitwriter *w, const unsigned char *p, size_t n)
{
    memcpy(w->data + w->length, p, n);
    w->length += n;
}

/* RFC 1951, 3.2.6's fixed codes */
static void fixed_lit(bitwriter *w, unsigned sym)
{
    if (sym < 144)
        put_code(w, 0x30 + sym, 8);
    else if (sym < 256)
        put_code(w, 0x190 + sym - 144, 9);
    else if (sym < 280)
        put_code(w, sym - 256, 7);
    else
        put_code(w, 0xC0 + sym - 280, 8);
}

/* a length (3 to 258) and a distance (1 to 32768) by RFC 1951's tables; the
 * symbol sent through lit, the distance's through dist */
static const unsigned lbase[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83,
                                    99, 115, 131, 163, 195, 227, 258 };
static const int lextra[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const unsigned dbase[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769,
                                    1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
static const int dextra[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12,
                                12, 13, 13 };

typedef struct {
    unsigned code[288];
    unsigned char len[288];
} codebook;

/* canonical codes from lengths (RFC 1951, 3.2.2, steps 1 to 3), done again here, apart from the decoder */
static void make_codes(codebook *c, const unsigned char *lengths, int n)
{
    unsigned count[16] = { 0 }, next[16];
    int i, bits;
    unsigned code = 0;
    for (i = 0; i < n; i++)
        count[lengths[i]]++;
    count[0] = 0;
    for (bits = 1; bits < 16; bits++) {
        code = (code + count[bits - 1]) << 1;
        next[bits] = code;
    }
    for (i = 0; i < n; i++) {
        c->len[i] = lengths[i];
        c->code[i] = lengths[i] ? next[lengths[i]]++ : 0;
    }
}

static void put_sym(bitwriter *w, const codebook *c, unsigned sym)
{
    put_code(w, c->code[sym], c->len[sym]);
}

static void put_copy(bitwriter *w, const codebook *lit, const codebook *dist, unsigned len, unsigned d,
                     int long_258)
{
    int i = 28, j = 29;
    while (lbase[i] > len)
        i--;
    if (long_258 && len == 258)
        i = 27;                     /* 284 with 31 extra: 258 the long way, as zlib accepts too */
    if (lit)
        put_sym(w, lit, 257 + (unsigned)i);
    else
        fixed_lit(w, 257 + (unsigned)i);
    put(w, len - lbase[i], lextra[i]);
    while (dbase[j] > d)
        j--;
    if (dist)
        put_sym(w, dist, (unsigned)j);
    else
        put_code(w, (unsigned)j, 5);
    put(w, d - dbase[j], dextra[j]);
}

/* A dynamic block's header: the code lengths sent plainly, each a 4-bit code
 * of the code lengths' code (symbols 0 to 15 all four bits long). */
static void dynamic_header(bitwriter *w, int last, const unsigned char *lit, int nlit, const unsigned char *dist,
                           int ndist)
{
    static const int order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
    int i;
    put(w, (unsigned long)last, 1);
    put(w, 2, 2);
    put(w, (unsigned long)(nlit - 257), 5);
    put(w, (unsigned long)(ndist - 1), 5);
    put(w, 15, 4);
    for (i = 0; i < 19; i++)
        put(w, order[i] >= 16 ? 0 : 4, 3);
    for (i = 0; i < nlit; i++)
        put_code(w, lit[i], 4);
    for (i = 0; i < ndist; i++)
        put_code(w, dist[i], 4);
}

static unsigned char want[140000];

/* our result and bytes; and zlib's, which must agree */
static void expect(const char *what, const bitwriter *w, size_t out_length, int result, const unsigned char *bytes)
{
    unsigned char *o, *t;
    int r = ours(w->data, w->length, out_length, &o), z = theirs(w->data, w->length, out_length, &t);
    CHECK(r == result, "%s: ours gave %d, not %d", what, r, result);
    CHECK(z == result, "%s: zlib gave %d, not %d (the test is wrong)", what, z, result);
    if (r == OWF_OK && result == OWF_OK && bytes && out_length)
        CHECK(memcmp(o, bytes, out_length) == 0, "%s: different bytes", what);
    free(o);
    free(t);
    /* cut short anywhere: refused */
    if (result == OWF_OK) {
        size_t cut;
        for (cut = 0; cut < w->length; cut += w->length > 400 ? w->length / 97 + 1 : 1) {
            r = ours(w->data, cut, out_length, &o);
            CHECK(r == OWF_ERR_CORRUPT, "%s cut at %lu: %d", what, (unsigned long)cut, r);
            free(o);
        }
    }
}

static void test_hand_made(void)
{
    static bitwriter w;
    static const unsigned char abc[] = "abc";
    unsigned char lit[288], dist[32];
    codebook lc, dc;
    size_t i;

    /* empty blocks */
    bw_init(&w);
    put(&w, 1, 1), put(&w, 0, 2), bw_flush(&w), put(&w, 0, 16), put(&w, 0xFFFF, 16);
    expect("empty stored block", &w, 0, OWF_OK, NULL);
    bw_init(&w);
    put(&w, 1, 1), put(&w, 1, 2), fixed_lit(&w, 256), bw_flush(&w);
    expect("empty fixed block", &w, 0, OWF_OK, NULL);
    bw_init(&w);   /* empty stored, empty fixed, then "abc" stored */
    put(&w, 0, 1), put(&w, 0, 2), bw_flush(&w), put(&w, 0, 16), put(&w, 0xFFFF, 16);
    put(&w, 0, 1), put(&w, 1, 2), fixed_lit(&w, 256);
    put(&w, 1, 1), put(&w, 0, 2), bw_flush(&w), put(&w, 3, 16), put(&w, 0xFFFC, 16), bw_bytes(&w, abc, 3);
    expect("empty blocks then abc", &w, 3, OWF_OK, abc);

    /* a stored block of 65,535 bytes, then another of one */
    for (i = 0; i < 65536; i++)
        want[i] = (unsigned char)(i * 7 + (i >> 8));
    bw_init(&w);
    put(&w, 0, 1), put(&w, 0, 2), bw_flush(&w), put(&w, 65535, 16), put(&w, 0, 16), bw_bytes(&w, want, 65535);
    put(&w, 1, 1), put(&w, 0, 2), bw_flush(&w), put(&w, 1, 16), put(&w, 0xFFFE, 16), bw_bytes(&w, want + 65535, 1);
    expect("stored 65,535 + 1", &w, 65536, OWF_OK, want);
    expect("stored 65,535 + 1 into 65,535", &w, 65535, OWF_ERR_CORRUPT, NULL);
    w.data[3] ^= 1;   /* NLEN no longer LEN's complement */
    expect("stored, NLEN wrong", &w, 65536, OWF_ERR_CORRUPT, NULL);

    /* a distance of 32,768: 32,768 literals, then the first three again */
    bw_init(&w);
    put(&w, 1, 1), put(&w, 1, 2);
    for (i = 0; i < 32768; i++)
        fixed_lit(&w, want[i]);
    put_copy(&w, NULL, NULL, 3, 32768, 0);
    fixed_lit(&w, 256), bw_flush(&w);
    memcpy(want + 32768, want, 3);
    expect("distance 32,768", &w, 32771, OWF_OK, want);
    bw_init(&w);   /* from 32,767 bytes in: before the start */
    put(&w, 1, 1), put(&w, 1, 2);
    for (i = 0; i < 32767; i++)
        fixed_lit(&w, want[i]);
    put_copy(&w, NULL, NULL, 3, 32768, 0);
    fixed_lit(&w, 256), bw_flush(&w);
    expect("distance 32,768 from 32,767", &w, 32770, OWF_ERR_CORRUPT, NULL);
    bw_init(&w);
    put(&w, 1, 1), put(&w, 1, 2), put_copy(&w, NULL, NULL, 3, 1, 0), fixed_lit(&w, 256), bw_flush(&w);
    expect("distance 1 at the start", &w, 3, OWF_ERR_CORRUPT, NULL);

    /* a length of 258, both ways it can be sent; and one past the end */
    memset(want, 'x', 259);
    for (i = 0; i < 2; i++) {
        bw_init(&w);
        put(&w, 1, 1), put(&w, 1, 2), fixed_lit(&w, 'x'), put_copy(&w, NULL, NULL, 258, 1, (int)i);
        fixed_lit(&w, 256), bw_flush(&w);
        expect(i ? "length 258 as 284 + 31" : "length 258", &w, 259, OWF_OK, want);
        expect("length 258 past the end", &w, 258, OWF_ERR_CORRUPT, NULL);
    }

    /* fixed codes that are never used: literals 286, 287; distances 30, 31 */
    for (i = 286; i < 288; i++) {
        bw_init(&w);
        put(&w, 1, 1), put(&w, 1, 2), fixed_lit(&w, 'a'), fixed_lit(&w, (unsigned)i), put_code(&w, 0, 5);
        fixed_lit(&w, 256), bw_flush(&w);
        expect("fixed literal 286/287", &w, 4, OWF_ERR_CORRUPT, NULL);
    }
    for (i = 30; i < 32; i++) {
        bw_init(&w);
        put(&w, 1, 1), put(&w, 1, 2), fixed_lit(&w, 'a'), fixed_lit(&w, 257), put_code(&w, (unsigned)i, 5);
        fixed_lit(&w, 256), bw_flush(&w);
        expect("fixed distance 30/31", &w, 4, OWF_ERR_CORRUPT, NULL);
    }
    bw_init(&w);
    put(&w, 1, 1), put(&w, 3, 2), put(&w, 0, 16), bw_flush(&w);
    expect("block type 3", &w, 0, OWF_ERR_CORRUPT, NULL);
    bw_init(&w);
    put(&w, 0, 1), put(&w, 1, 2), fixed_lit(&w, 'a'), fixed_lit(&w, 256), bw_flush(&w);
    expect("no last block", &w, 1, OWF_ERR_CORRUPT, NULL);
    bw_init(&w);
    put(&w, 1, 1), put(&w, 1, 2), fixed_lit(&w, 'a'), fixed_lit(&w, 'b'), fixed_lit(&w, 256), bw_flush(&w);
    expect("a byte more than the directory says", &w, 1, OWF_ERR_CORRUPT, NULL);
    expect("a byte fewer than the directory says", &w, 3, OWF_ERR_CORRUPT, NULL);

    /* dynamic: only the end code (a single code of one bit), no distances */
    memset(lit, 0, sizeof lit), memset(dist, 0, sizeof dist);
    lit[256] = 1;
    make_codes(&lc, lit, 257);
    bw_init(&w);
    dynamic_header(&w, 1, lit, 257, dist, 1);
    put_sym(&w, &lc, 256), bw_flush(&w);
    expect("dynamic, the end code alone", &w, 0, OWF_OK, NULL);
    bw_init(&w);   /* the single code's unused bit pattern */
    dynamic_header(&w, 1, lit, 257, dist, 1);
    put(&w, 1, 1), bw_flush(&w);
    expect("dynamic, the end code alone, then the unused code", &w, 0, OWF_ERR_CORRUPT, NULL);

    /* dynamic: literals and copies with a single distance code of one bit */
    memset(lit, 0, sizeof lit);
    lit['a'] = 2, lit['b'] = 2, lit[256] = 2, lit[258] = 2;   /* 258: length 4 */
    dist[0] = 1;                                              /* distance 1 */
    make_codes(&lc, lit, 259);
    make_codes(&dc, dist, 1);
    bw_init(&w);
    dynamic_header(&w, 1, lit, 259, dist, 1);
    put_sym(&w, &lc, 'a'), put_sym(&w, &lc, 'b'), put_copy(&w, &lc, &dc, 4, 1, 0), put_sym(&w, &lc, 256);
    bw_flush(&w);
    expect("dynamic, one distance code", &w, 6, OWF_OK, (const unsigned char *)"abbbbb");
    bw_init(&w);
    dynamic_header(&w, 1, lit, 259, dist, 1);
    put_sym(&w, &lc, 'a'), put_sym(&w, &lc, 'b'), put_sym(&w, &lc, 258), put(&w, 1, 1), put_sym(&w, &lc, 256);
    bw_flush(&w);
    expect("dynamic, one distance code, the unused one sent", &w, 6, OWF_ERR_CORRUPT, NULL);
    dist[0] = 0;   /* no distance codes at all: literals only */
    bw_init(&w);
    dynamic_header(&w, 1, lit, 259, dist, 1);
    put_sym(&w, &lc, 'a'), put_sym(&w, &lc, 'b'), put_sym(&w, &lc, 256), bw_flush(&w);
    expect("dynamic, no distance codes", &w, 2, OWF_OK, (const unsigned char *)"ab");
    bw_init(&w);
    dynamic_header(&w, 1, lit, 259, dist, 1);
    put_sym(&w, &lc, 'a'), put_sym(&w, &lc, 258), put_sym(&w, &lc, 256), bw_flush(&w);
    expect("dynamic, no distance codes, a copy", &w, 5, OWF_ERR_CORRUPT, NULL);

    /* codes that break the rules */
    memset(lit, 0, sizeof lit), memset(dist, 0, sizeof dist);
    lit['a'] = 1, lit['b'] = 1, lit[256] = 1;   /* three codes of one bit: over-subscribed */
    dist[0] = 1, dist[1] = 1;
    bw_init(&w);
    dynamic_header(&w, 1, lit, 257, dist, 2);
    put(&w, 0, 8), bw_flush(&w);
    expect("dynamic, literal code over-subscribed", &w, 1, OWF_ERR_CORRUPT, NULL);
    lit['b'] = 0, lit['a'] = 2, lit[256] = 2;   /* two codes of two bits: incomplete */
    bw_init(&w);
    dynamic_header(&w, 1, lit, 257, dist, 2);
    put(&w, 0, 8), bw_flush(&w);
    expect("dynamic, literal code incomplete", &w, 1, OWF_ERR_CORRUPT, NULL);
    lit['a'] = 1, lit[256] = 1;
    dist[0] = 2, dist[1] = 2;
    bw_init(&w);
    dynamic_header(&w, 1, lit, 257, dist, 2);
    put(&w, 0, 8), bw_flush(&w);
    expect("dynamic, distance code incomplete", &w, 1, OWF_ERR_CORRUPT, NULL);
    dist[0] = 1, dist[1] = 1, dist[2] = 1;
    bw_init(&w);
    dynamic_header(&w, 1, lit, 257, dist, 3);
    put(&w, 0, 8), bw_flush(&w);
    expect("dynamic, distance code over-subscribed", &w, 1, OWF_ERR_CORRUPT, NULL);
    lit[256] = 0, lit['b'] = 1;
    dist[2] = 0;
    bw_init(&w);
    dynamic_header(&w, 1, lit, 257, dist, 2);
    put(&w, 0, 8), bw_flush(&w);
    expect("dynamic, no end code", &w, 1, OWF_ERR_CORRUPT, NULL);
    lit['b'] = 0, lit[256] = 1;
    bw_init(&w);   /* 287 literal codes */
    dynamic_header(&w, 1, lit, 287, dist, 2);
    put(&w, 0, 8), bw_flush(&w);
    expect("dynamic, 287 literal codes", &w, 1, OWF_ERR_CORRUPT, NULL);
    bw_init(&w);   /* 31 distance codes */
    dynamic_header(&w, 1, lit, 257, dist, 31);
    put(&w, 0, 8), bw_flush(&w);
    expect("dynamic, 31 distance codes", &w, 1, OWF_ERR_CORRUPT, NULL);

    /* the code lengths' code: incomplete, over-subscribed; repeats wrong */
    {
        static const int order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
        int t, k;
        for (t = 0; t < 4; t++) {
            /* code lengths' code: symbols 0, 1, 16, 18 two bits each (complete), except as the case says */
            unsigned char cl[19] = { 0 };
            codebook cc;
            cl[0] = 2, cl[1] = 2, cl[16] = 2, cl[18] = 2;
            if (t == 0)
                cl[18] = 0;            /* incomplete */
            if (t == 1)
                cl[17] = 2;            /* over-subscribed */
            make_codes(&cc, cl, 19);
            bw_init(&w);
            put(&w, 1, 1), put(&w, 2, 2), put(&w, 0, 5), put(&w, 0, 5), put(&w, 15, 4);
            for (k = 0; k < 19; k++)
                put(&w, cl[order[k]], 3);
            if (t == 2) {             /* 16 (repeat the last) first */
                put_sym(&w, &cc, 16), put(&w, 0, 2);
            } else if (t == 3) {      /* 18: 138 zeros, then more than 258 in all */
                put_sym(&w, &cc, 18), put(&w, 127, 7), put_sym(&w, &cc, 18), put(&w, 127, 7);
            } else {
                put_sym(&w, &cc, 1);
            }
            put(&w, 0, 32), bw_flush(&w);
            expect(t == 0 ? "code lengths' code incomplete" : t == 1 ? "code lengths' code over-subscribed"
                   : t == 2 ? "repeat with nothing before" : "repeat past the end", &w, 1, OWF_ERR_CORRUPT, NULL);
        }
        /* the same code, sent right: 'a' and the end code one bit each, a distance code */
        {
            unsigned char cl[19] = { 0 };
            codebook cc;
            cl[0] = 2, cl[1] = 2, cl[16] = 2, cl[18] = 2;
            make_codes(&cc, cl, 19);
            bw_init(&w);
            put(&w, 1, 1), put(&w, 2, 2), put(&w, 0, 5), put(&w, 0, 5), put(&w, 15, 4);
            for (k = 0; k < 19; k++)
                put(&w, cl[order[k]], 3);
            put_sym(&w, &cc, 18), put(&w, 'a' - 11, 7);           /* 97 zeros */
            put_sym(&w, &cc, 1);                                  /* 'a' */
            put_sym(&w, &cc, 18), put(&w, 138 - 11, 7);           /* 98 to 235 */
            put_sym(&w, &cc, 18), put(&w, 20 - 11, 7);            /* 236 to 255 */
            put_sym(&w, &cc, 1);                                  /* the end code */
            put_sym(&w, &cc, 1);                                  /* the distance */
            put(&w, 0, 1), put(&w, 0, 1), put(&w, 1, 1);          /* 'a', 'a', end */
            bw_flush(&w);
            expect("repeats of zeros, right", &w, 2, OWF_OK, (const unsigned char *)"aa");
        }
    }
}

/* ---- 3. damaged streams, against zlib ---- */

#define POOL 96

static void test_damaged(int rounds)
{
    unsigned char *data = malloc(70000), *pool[POOL];
    size_t pool_packed[POOL], pool_length[POOL];
    int round, agree = 0, accepted = 0, p;

    /* whole streams of every kind and level, to damage copies of */
    for (p = 0; p < POOL; p++) {
        pool_length[p] = below(p % 4 ? 3000 : 70000);
        make_data(p % NKINDS, data, pool_length[p]);
        pool[p] = zdeflate(data, pool_length[p], p % 10, 15, 8, p % 5 ? Z_DEFAULT_STRATEGY : Z_FIXED, 0, Z_NO_FLUSH,
                           &pool_packed[p]);
    }
    for (round = 0; round < rounds; round++) {
        int from = (int)below(POOL), kind = from % NKINDS, level = from % 10, how = (int)below(6), r, q;
        size_t length = pool_length[from], packed = pool_packed[from], out_length = length, i;
        unsigned char *z = malloc(packed > 2000 ? packed : 2000), *o, *t;

        if (!z)
            exit(2);
        memcpy(z, pool[from], packed);
        switch (how) {
        case 0:   /* bits flipped */
            for (i = 1 + below(3); i && packed; i--)
                z[below(packed)] ^= (unsigned char)(1 << below(8));
            break;
        case 1:   /* a byte changed */
            if (packed)
                z[below(packed)] = (unsigned char)next_random();
            break;
        case 2:   /* cut short */
            packed = below(packed);
            break;
        case 3:   /* the wrong size */
            out_length = below(length + 2);
            break;
        case 4:   /* a flip near the start, where the codes are */
            if (packed)
                z[below(packed < 64 ? packed : 64)] ^= (unsigned char)(1 << below(8));
            break;
        default:  /* noise */
            packed = below(2000);
            for (i = 0; i < packed; i++)
                z[i] = (unsigned char)next_random();
            out_length = below(4000);
            break;
        }
        r = ours(z, packed, out_length, &o);
        q = theirs(z, packed, out_length, &t);
        if (r == q && (r != OWF_OK || out_length == 0 || memcmp(o, t, out_length) == 0))
            agree++;
        else
            CHECK(0, "damaged stream %d (kind %d, level %d, change %d): ours %d, zlib %d", round, kind, level, how,
                  r, q);
        CHECK(r == OWF_OK || r == OWF_ERR_CORRUPT, "damaged stream %d: result %d", round, r);
        accepted += r == OWF_OK;
        free(o);
        free(t);
        free(z);
    }
    printf("%d damaged streams: %d read as zlib reads them (%d still whole)\n", rounds, agree, accepted);
    for (p = 0; p < POOL; p++)
        free(pool[p]);
    free(data);
}

/* ---- the times, on real zip files ---- */

#define LE16(p) ((unsigned)((p)[0] | ((p)[1] << 8)))
#define LE32(p) ((unsigned long)(p)[0] | ((unsigned long)(p)[1] << 8) | ((unsigned long)(p)[2] << 16) | \
                 ((unsigned long)(p)[3] << 24))

static double now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static void bench(int argc, char **argv)
{
    int a;
    double total_ours = 0, total_zlib = 0, total_bytes = 0;
    for (a = 0; a < argc; a++) {
        FILE *f = fopen(argv[a], "rb");
        unsigned char *d;
        long n;
        size_t i, entries = 0;
        double bytes = 0, t_ours, t_zlib;
        int reps, k;
        const unsigned char *dir = NULL;

        if (!f || fseek(f, 0, SEEK_END) || (n = ftell(f)) < 22 || fseek(f, 0, SEEK_SET)) {
            printf("%s: cannot read\n", argv[a]);
            failures++;
            if (f)
                fclose(f);
            continue;
        }
        d = malloc((size_t)n);
        if (!d || fread(d, 1, (size_t)n, f) != (size_t)n) {
            printf("%s: cannot read\n", argv[a]);
            exit(2);
        }
        fclose(f);
        for (i = (size_t)n - 22; i > 0 && LE32(d + i) != 0x06054B50UL; i--)
            ;
        dir = d + LE32(d + i + 16);
        entries = LE16(d + i + 10);
        /* enough rounds for 100 MB each way */
        for (k = 0, i = 0; k < (int)entries; k++) {
            const unsigned char *e = dir + i;
            if (LE16(e + 10) == 8)
                bytes += (double)LE32(e + 24);
            i += 46 + LE16(e + 28) + LE16(e + 30) + LE16(e + 32);
        }
        reps = bytes > 0 ? (int)(100e6 / bytes) + 1 : 0;
        for (t_ours = t_zlib = 0, k = 0; k < 2; k++) {
            double start = now();
            int rep;
            for (rep = 0; rep < reps; rep++) {
                size_t j = 0;
                int m;
                for (m = 0; m < (int)entries; m++) {
                    const unsigned char *e = dir + j, *local = d + LE32(e + 42), *body;
                    unsigned long packed = LE32(e + 20), size = LE32(e + 24);
                    j += 46 + LE16(e + 28) + LE16(e + 30) + LE16(e + 32);
                    if (LE16(e + 10) != 8)
                        continue;
                    body = local + 30 + LE16(local + 26) + LE16(local + 28);
                    {
                        unsigned char *o = malloc(size + 1);
                        int r;
                        if (k == 0) {
                            r = owf_inflate(o, size, body, packed);
                        } else {
                            z_stream z;
                            memset(&z, 0, sizeof z);
                            inflateInit2(&z, -15);
                            z.next_in = (Bytef *)body;
                            z.avail_in = (uInt)packed;
                            z.next_out = o;
                            z.avail_out = (uInt)size;
                            r = inflate(&z, Z_FINISH) == Z_STREAM_END && z.total_out == size ? OWF_OK : -1;
                            inflateEnd(&z);
                        }
                        if (r != OWF_OK || crc32(0, o, (uInt)size) != LE32(e + 16)) {
                            printf("%s: an entry did not inflate (%s)\n", argv[a], k ? "zlib" : "ours");
                            failures++;
                            rep = reps;
                        }
                        free(o);
                    }
                }
            }
            if (k == 0)
                t_ours = now() - start;
            else
                t_zlib = now() - start;
        }
        printf("%-40s %8.0f bytes inflated: ours %7.1f MB/s, zlib %7.1f MB/s, zlib %.2fx as fast\n", argv[a], bytes,
               bytes * reps / t_ours / 1e6, bytes * reps / t_zlib / 1e6, t_ours / t_zlib);
        total_ours += t_ours / reps;
        total_zlib += t_zlib / reps;
        total_bytes += bytes;
        free(d);
    }
    if (argc > 1)
        printf("%-40s %8.0f bytes inflated: ours %7.1f MB/s, zlib %7.1f MB/s, zlib %.2fx as fast\n", "all",
               total_bytes, total_bytes / total_ours / 1e6, total_bytes / total_zlib / 1e6, total_ours / total_zlib);
}

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "--bench") == 0) {
        bench(argc - 2, argv + 2);
        return failures ? 1 : 0;
    }
    printf("owf_inflate: state %lu bytes\n", (unsigned long)owf_inflate_state_size);
    test_hand_made();
    test_zlib();
    test_damaged(argc > 1 ? atoi(argv[1]) : 20000);
    printf("%s: %d of %d checks failed\n", failures ? "FAILED" : "inflate tests passed", failures, checks);
    return failures ? 1 : 0;
}
