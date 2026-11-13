/*
 * libowf's own inflate (owf_inflate.c): raw DEFLATE (RFC 1951), as zip
 * entries hold it, with no z.library and no zlib.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef OWF_INFLATE_H
#define OWF_INFLATE_H

#include <stddef.h>

/* Inflates the raw DEFLATE stream in (in_length bytes) into out, which it
 * must fill exactly: out_length bytes, the size the zip's directory gives.
 * OWF_OK; OWF_ERR_CORRUPT when the stream is damaged or cut short, refers
 * back before its start, or makes more or fewer bytes than out_length;
 * OWF_ERR_MEMORY when its state cannot be had. Bytes after the last block
 * are ignored. */
int owf_inflate(unsigned char *out, size_t out_length, const unsigned char *in, size_t in_length);

/* As owf_inflate, for a stream whose size is not known: it may make fewer
 * than out_length bytes, and *made says how many. A stream that needs more
 * room than out_length fails OWF_ERR_CORRUPT, as a damaged one does, so a
 * caller that does not know the size tries again with more room up to a
 * limit of its own (DEFLATE makes at most 1032 bytes from one). */
int owf_inflate_upto(unsigned char *out, size_t out_length, const unsigned char *in, size_t in_length,
                     size_t *made);

/* The state's size in bytes: one block from malloc for the length of a
 * call, the same whatever the stream. */
extern const size_t owf_inflate_state_size;

#endif
