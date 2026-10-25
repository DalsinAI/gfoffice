/*
 * libowf: zip archives read, and raw DEFLATE inflated, for programs: what
 * libowf's own readers use for ODT, ODS, DOCX, XLSX and the rest, as
 * owf.library 2's calls (and linked in, as plain functions).
 *
 * Deflated members inflate through z.library when it is in LIBS: (zlib on
 * the PC), and with libowf's own inflate (owf_inflate.c, RFC 1951) when it
 * is not, so a program reads any zip package on any install.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#ifndef OWF_ZIP_H
#define OWF_ZIP_H

#include <stddef.h>

#include "owf.h"

#ifdef __cplusplus
extern "C" {
#endif

/* An archive in memory. The program keeps it (on its stack is fine) and
 * the data, which must outlive it. Part of owf.library's ABI: its size is
 * OWF_STRUCT_UNZIP's (owf_struct_size). */
typedef struct owf_unzip {
    const unsigned char *data;
    size_t length;
    const unsigned char *dir;    /* the central directory */
    unsigned entries;
} owf_unzip;

/* 1 when data is a zip we can read, 0 when not. */
int owf_unzip_open(owf_unzip *zip, const unsigned char *data, size_t length);
/* 1 when the archive holds a file of that name. */
int owf_unzip_has(const owf_unzip *zip, const char *name);
/* One file, stored or deflated, into memory from owf_malloc with a NUL
 * after it (owf_free it). OWF_OK; OWF_ERR_FORMAT when there is no such
 * file; OWF_ERR_UNSUPPORTED (encrypted, or another method);
 * OWF_ERR_CORRUPT (damaged, a wrong CRC, or bigger than 64 MB);
 * OWF_ERR_MEMORY. */
int owf_unzip_read(const owf_unzip *zip, const char *name, unsigned char **data, size_t *length);

/* Inflates the raw DEFLATE stream in (in_length bytes) into out, which it
 * must fill exactly: out_length bytes. OWF_OK; OWF_ERR_CORRUPT when the
 * stream is damaged or cut short, or makes more or fewer bytes;
 * OWF_ERR_MEMORY. libowf's own, never z.library; its state (under 2 KB)
 * is allocated for the call. */
int owf_inflate(unsigned char *out, size_t out_length, const unsigned char *in, size_t in_length);

#ifdef __cplusplus
}
#endif

/* owf.library's calls, once every header with calls is complete (owf.h) */
#define OWF_ZIP_H_DONE
#if defined(OWF_USE_LIBRARY) && !defined(OWF_LIBRARY_BUILD) && defined(OWF_H_DONE) && \
    defined(OWF_CALC_H_DONE) && defined(OWF_PUB_H_DONE)
#include <proto/owf.h>
#endif

#endif
