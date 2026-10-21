/*
 * libowf's deflate and inflate. On the PC (the tests) they are the system's
 * zlib. On the Amiga (OWF_ZLIBRARY) they are z.library, ours from
 * openzlib: its calls in libowf/amiga/sdk (made from z_lib.sfd), opened
 * the first time libowf needs it. Without z.library, ODT, DOCX and PNG in
 * PDF give OWF_ERR_UNSUPPORTED; plain and Amiga formats still work.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#ifndef OWF_ZLIB_H
#define OWF_ZLIB_H

#ifndef OWF_ZLIBRARY

#include <zlib.h>

#else

#include <exec/types.h>

/* zlib.h's types and z_stream, as the 68k lays them out (56 bytes), which
 * is what z.library takes. */
typedef unsigned char Byte;
typedef Byte Bytef;
typedef unsigned int uInt;
typedef unsigned long uLong;
typedef uLong uLongf;
typedef void *voidpf;

typedef struct z_stream_s {
    const Bytef *next_in;
    uInt avail_in;
    uLong total_in;
    Bytef *next_out;
    uInt avail_out;
    uLong total_out;
    const char *msg;
    void *state;
    void *zalloc;
    void *zfree;
    voidpf opaque;
    int data_type;
    uLong adler;
    uLong reserved;
} z_stream;

#define Z_NO_FLUSH 0
#define Z_SYNC_FLUSH 2
#define Z_FULL_FLUSH 3
#define Z_FINISH 4
#define Z_OK 0
#define Z_STREAM_END 1
#define Z_NEED_DICT 2
#define Z_ERRNO (-1)
#define Z_STREAM_ERROR (-2)
#define Z_DATA_ERROR (-3)
#define Z_MEM_ERROR (-4)
#define Z_BUF_ERROR (-5)
#define Z_VERSION_ERROR (-6)
#define Z_DEFAULT_COMPRESSION (-1)
#define Z_DEFAULT_STRATEGY 0
#define Z_DEFLATED 8
#define Z_NULL 0

/* 1 once z.library is open (owf_zlib_amiga.c). */
int owf_z_ready(void);
void owf_z_close(void);

#include <proto/z.h>

#define inflateInit2(s, w) (owf_z_ready() ? (int)InflateInit2((s), (w)) : Z_VERSION_ERROR)
#define inflate(s, f) ((int)Inflate((s), (f)))
#define inflateEnd(s) ((int)InflateEnd(s))
#define deflateInit2(s, l, m, w, ml, st) \
    (owf_z_ready() ? (int)DeflateInit2((s), (l), (m), (w), (ml), (st)) : Z_VERSION_ERROR)
#define deflate(s, f) ((int)Deflate((s), (f)))
#define deflateEnd(s) ((int)DeflateEnd(s))
#define deflateBound(s, n) ((uLong)DeflateBound((s), (n)))
#define compressBound(n) (owf_z_ready() ? (uLong)CompressBound(n) : (uLong)(n) + (uLong)(n) / 1000 + 64)
#define compress2(d, dl, s, sl, l) \
    (owf_z_ready() ? (int)Compress2((d), (ULONG *)(dl), (s), (sl), (l)) : Z_VERSION_ERROR)
#define uncompress(d, dl, s, sl) \
    (owf_z_ready() ? (int)Uncompress((d), (ULONG *)(dl), (s), (sl)) : Z_VERSION_ERROR)

#endif

#endif
