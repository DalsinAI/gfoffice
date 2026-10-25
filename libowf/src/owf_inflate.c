/*
 * libowf's own inflate: raw DEFLATE (RFC 1951), so that zip packages (ODT,
 * DOCX, and the workbooks and decks to come) open without z.library.
 * Written from the RFC alone: stored, fixed and dynamic blocks, distances
 * up to 32 KB.
 *
 * The whole entry goes into the caller's buffer, whose size the zip's
 * directory gives, so a back-reference copies from what is already there
 * and no window is kept. Every read and write is checked against its end,
 * and every code against RFC 1951's rules, so damaged input gives
 * OWF_ERR_CORRUPT, never a crash. The memory is one fixed-size state from
 * malloc (the Amiga's stacks are small), and there is no recursion.
 *
 * Small rather than fast: codes are read a bit at a time (RFC 1951,
 * 3.2.2), with no lookup tables. z.library, when there is one, is the
 * faster path (owf_unzip.c).
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "owf_inflate.h"
#include "owf.h"

#include <stdlib.h>
#include <string.h>

/* The block headers and code building are rare beside the codes loop:
 * GCC makes them small rather than fast. */
#ifdef __GNUC__
#define COLD __attribute__((cold))
#else
#define COLD
#endif

typedef struct {
    unsigned short count[16];          /* how many codes have each length */
    unsigned short symbol[288];        /* the symbols in code order: by length, then value */
} huffman;

typedef struct {
    const unsigned char *in, *end;
    unsigned long bits;                /* read and not yet used, the next in bit 0 */
    unsigned nbits;
} bitreader;

typedef struct {
    bitreader r;
    unsigned char *start, *out, *end;
    huffman lit, dist;                 /* literals and lengths (first the code lengths' code), distances */
    unsigned char lengths[286 + 30];
} inflater;

const size_t owf_inflate_state_size = sizeof(inflater);

/* RFC 1951, 3.2.5: lengths 3 to 258 (codes 257 to 285), distances 1 to 32768 */
static const unsigned short length_base[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
};
static const unsigned char length_extra[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
};
static const unsigned short dist_base[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769,
    1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577
};
static const unsigned char dist_extra[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
};

/* At least 25 bits in hand, or all the input there is. */
static void fill(bitreader *r)
{
    while (r->nbits <= 24 && r->in < r->end) {
        r->bits |= (unsigned long)*r->in++ << r->nbits;
        r->nbits += 8;
    }
}

/* The next n bits (0 to 13), first bit lowest; -1 when the input has ended. */
static int bits(bitreader *r, unsigned n)
{
    int v;
    if (r->nbits < n) {
        fill(r);
        if (r->nbits < n)
            return -1;
    }
    v = (int)(r->bits & ((1UL << n) - 1));
    r->bits >>= n;
    r->nbits -= n;
    return v;
}

/* The next symbol of h; -1 for bits that are no code, or input that has
 * ended. A code comes first bit first; the codes of each length are
 * consecutive numbers, following on from the shorter ones' (RFC 1951,
 * 3.2.2), so each length needs one comparison. */
static int decode(bitreader *r, const huffman *h)
{
    unsigned len, code = 0, first = 0, index = 0;
    unsigned long b;

    if (r->nbits < 15)
        fill(r);
    b = r->bits;
    for (len = 1; len <= 15 && len <= r->nbits; len++) {
        code |= (unsigned)(b & 1);
        b >>= 1;
        if (code - first < h->count[len]) {
            r->bits >>= len;
            r->nbits -= len;
            return h->symbol[index + code - first];
        }
        index += h->count[len];
        first = (first + h->count[len]) << 1;
        code <<= 1;
    }
    return -1;
}

/* Makes h from n symbols' code lengths. Returns how much of the code space
 * is left unused, in 15-bit codes: 0 for a complete code, 1 << 14 for a
 * single code of one bit, 1 << 15 for no codes at all; -1 when the lengths
 * ask for more codes than there are. */
COLD static long build(huffman *h, const unsigned char *lengths, unsigned n)
{
    unsigned short next[16];
    unsigned len, sym;
    long left = 1;

    memset(h->count, 0, sizeof h->count);
    for (sym = 0; sym < n; sym++)
        h->count[lengths[sym]]++;
    next[1] = 0;
    for (len = 1; len < 16; len++) {
        left = (left << 1) - h->count[len];
        if (left < 0)
            return -1;
        if (len < 15)
            next[len + 1] = (unsigned short)(next[len] + h->count[len]);
    }
    for (sym = 0; sym < n; sym++)
        if (lengths[sym])
            h->symbol[next[lengths[sym]]++] = (unsigned short)sym;
    return left;
}

/* A compressed block's literals and copies, up to its end code. The bit
 * reader and the output pointer are held in locals for the loop, so that
 * a store to the output need not make the compiler read them again. */
static int codes(inflater *s)
{
    bitreader r = s->r;
    unsigned char *out = s->out, *const start = s->start, *const end = s->end;
    int result = OWF_ERR_CORRUPT;

    for (;;) {
        int sym = decode(&r, &s->lit), extra;
        unsigned len, dist;
        unsigned char *from;

        if (sym < 256) {
            if (sym < 0 || out == end)
                break;
            *out++ = (unsigned char)sym;
            continue;
        }
        if (sym == 256) {
            result = OWF_OK;
            break;
        }
        sym -= 257;
        if (sym >= 29)                      /* 286 and 287 are never used */
            break;
        extra = bits(&r, length_extra[sym]);
        if (extra < 0)
            break;
        len = length_base[sym] + (unsigned)extra;
        sym = decode(&r, &s->dist);
        if (sym < 0 || sym >= 30)           /* nor are distances 30 and 31 */
            break;
        extra = bits(&r, dist_extra[sym]);
        if (extra < 0)
            break;
        dist = dist_base[sym] + (unsigned)extra;
        if (dist > (size_t)(out - start) || len > (size_t)(end - out))
            break;                          /* before the start, or past the end */
        from = out - dist;
        do                                  /* byte by byte: the copy may overlap itself */
            *out++ = *from++;
        while (--len);
    }
    s->r = r;
    s->out = out;
    return result;
}

COLD static int stored(inflater *s)
{
    bitreader *r = &s->r;
    unsigned len;

    /* the rest of this byte is dropped; whole bytes in hand go back */
    r->in -= r->nbits >> 3;
    r->bits = 0;
    r->nbits = 0;
    if (r->end - r->in < 4)
        return OWF_ERR_CORRUPT;
    len = r->in[0] | (unsigned)r->in[1] << 8;
    if ((r->in[2] | (unsigned)r->in[3] << 8) != (~len & 0xFFFF))
        return OWF_ERR_CORRUPT;
    r->in += 4;
    if (len > (size_t)(r->end - r->in) || len > (size_t)(s->end - s->out))
        return OWF_ERR_CORRUPT;
    if (len) {
        memcpy(s->out, r->in, len);
        s->out += len;
        r->in += len;
    }
    return OWF_OK;
}

COLD static int fixed(inflater *s)
{
    unsigned i;
    for (i = 0; i < 288; i++)
        s->lengths[i] = (unsigned char)(i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8);
    build(&s->lit, s->lengths, 288);
    /* 32 codes of five bits make a complete code; 30 and 31 are refused as they come */
    memset(s->lengths, 5, 32);
    build(&s->dist, s->lengths, 32);
    return codes(s);
}

COLD static int dynamic(inflater *s)
{
    static const unsigned char order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
    static const unsigned char repeat_bits[3] = { 2, 3, 7 }, repeat_base[3] = { 3, 3, 11 };
    bitreader *r = &s->r;
    int nlit = bits(r, 5), ndist = bits(r, 5), ncode = bits(r, 4), i, n, v;
    long left;

    if ((nlit | ndist | ncode) < 0)
        return OWF_ERR_CORRUPT;
    nlit += 257;
    ndist += 1;
    ncode += 4;
    if (nlit > 286 || ndist > 30)
        return OWF_ERR_CORRUPT;
    for (i = 0; i < 19; i++) {
        v = i < ncode ? bits(r, 3) : 0;
        if (v < 0)
            return OWF_ERR_CORRUPT;
        s->lengths[order[i]] = (unsigned char)v;
    }
    if (build(&s->lit, s->lengths, 19) != 0)    /* the code lengths' own code must be complete */
        return OWF_ERR_CORRUPT;
    /* the literal and distance code lengths, as one run (a repeat may cross from one to the other) */
    for (i = 0; i < nlit + ndist;) {
        int sym = decode(r, &s->lit);
        if (sym < 16) {
            if (sym < 0)
                return OWF_ERR_CORRUPT;
            s->lengths[i++] = (unsigned char)sym;
            continue;
        }
        sym -= 16;                              /* 16: the last length again; 17 and 18: zeros */
        if (sym == 0 && i == 0)
            return OWF_ERR_CORRUPT;
        v = sym ? 0 : s->lengths[i - 1];
        n = bits(r, repeat_bits[sym]);
        if (n < 0 || i + (n += repeat_base[sym]) > nlit + ndist)
            return OWF_ERR_CORRUPT;
        while (n--)
            s->lengths[i++] = (unsigned char)v;
    }
    if (!s->lengths[256])                       /* no end of block */
        return OWF_ERR_CORRUPT;
    /* complete codes; or a single code of one bit (RFC 1951, 3.2.7); or,
     * for distances, none (a block of literals only) */
    left = build(&s->lit, s->lengths, (unsigned)nlit);
    if (left && (left != 1L << 14 || s->lit.count[1] != 1))
        return OWF_ERR_CORRUPT;
    left = build(&s->dist, s->lengths + nlit, (unsigned)ndist);
    if (left && left != 1L << 15 && (left != 1L << 14 || s->dist.count[1] != 1))
        return OWF_ERR_CORRUPT;
    return codes(s);
}

int owf_inflate(unsigned char *out, size_t out_length, const unsigned char *in, size_t in_length)
{
    inflater *s = malloc(sizeof *s);
    int last, type, result;

    if (!s)
        return OWF_ERR_MEMORY;
    s->r.in = in;
    s->r.end = in + in_length;
    s->r.bits = 0;
    s->r.nbits = 0;
    s->start = s->out = out;
    s->end = out + out_length;
    do {
        last = bits(&s->r, 1);
        type = bits(&s->r, 2);
        result = OWF_ERR_CORRUPT;
        if ((last | type) < 0)
            break;
        if (type == 0)
            result = stored(s);
        else if (type == 1)
            result = fixed(s);
        else if (type == 2)
            result = dynamic(s);
    } while (result == OWF_OK && !last);
    if (result == OWF_OK && s->out != s->end)
        result = OWF_ERR_CORRUPT;               /* fewer bytes than the directory said */
    free(s);
    return result;
}
