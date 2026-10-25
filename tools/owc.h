/*
 * OWConvert's parts (tools/owconvert.c), one file per GFOffice library
 * (docs/suite/owf-libraries.md, section 5): owc_doc.c on owfwrite.library,
 * owc_book.c on owfcalc.library, owc_deck.c on owfpresent.library and
 * owc_pub.c on owfpub.library, each built for its library (owf_proto.h:
 * one library's calls per file). With libowf linked in they are built
 * together, with no library. Each part opens its library when it is first
 * asked for, so a conversion loads only the libraries it needs.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#ifndef OWC_H
#define OWC_H

#include <stddef.h>

#ifdef __amigaos__
#include <dos/dos.h>
#define RC_OK RETURN_OK
#define RC_ERROR RETURN_ERROR
#define RC_FAIL RETURN_FAIL
#else
#define RC_OK 0
#define RC_ERROR 10
#define RC_FAIL 20
#endif

/* owconvert.c's */
int owc_ends_with(const char *s, const char *tail);
/* "Note", "Approximated", "Left out" */
const char *owc_level_name(int level);
/* a part's library missing: says so (once per library: each part asks once); 1 */
int owc_missing(const char *library, int version, const char *what);

/* Each part: open (1, or 0 when its library is missing: said already), close. */
int owc_doc_open(void);
void owc_doc_close(void);
int owc_book_open(void);
void owc_book_close(void);
int owc_deck_open(void);
void owc_deck_close(void);
int owc_pub_open(void);
void owc_pub_close(void);

/* What a file is (owf_file_kind, owfwrite.library's): an owf_kind. */
int owc_doc_kind(const unsigned char *data, size_t length);
/* 1 when the part's formats name format (format given), or write a file named to (format NULL) */
int owc_doc_writes(const char *to, const char *format);
int owc_book_writes(const char *to, const char *format);
int owc_deck_names(const char *format);
/* 1 when a presentation reader takes these bytes */
int owc_deck_reads(const unsigned char *data, size_t length);

/* The conversions: an AmigaDOS return code. */
int owc_doc_convert(const char *from, const char *to, const char *format, int show_report);
int owc_book_convert(const char *from, const char *to, const char *format, int show_report);
int owc_deck_convert(const unsigned char *data, size_t length, const char *from, const char *to,
                     const char *format, int show_report);
int owc_pub_convert(const char *from, int show_report);

/* FORMATS: each part's table */
void owc_doc_formats(void);
void owc_book_formats(void);
void owc_deck_formats(void);

#endif
