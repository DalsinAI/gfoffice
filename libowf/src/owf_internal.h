/*
 * libowf's own helpers: growing buffers, UTF-8 and character sets, IFF
 * chunks, zip writing. Not part of the public interface.
 * MIT, Copyright (c) 2026 Dalsin Limited.
 */
#ifndef OWF_INTERNAL_H
#define OWF_INTERNAL_H

#include "owf.h"

#include <stdarg.h>
#include <stddef.h>

/* ---- A growing byte buffer; after a failed allocation it stays failed ---- */

typedef struct {
    unsigned char *data;
    size_t length, capacity;
    int failed;
} owf_buf;

void owf_buf_init(owf_buf *buf);
void owf_buf_free(owf_buf *buf);
void owf_buf_put(owf_buf *buf, const void *data, size_t length);
void owf_buf_putc(owf_buf *buf, int c);
void owf_buf_puts(owf_buf *buf, const char *s);
void owf_buf_printf(owf_buf *buf, const char *fmt, ...);
/* Twips as points ("12.5pt"), without floating point. */
void owf_put_points(owf_buf *out, int twips);
/* UTF-8 text with &, <, > and " escaped for XML and HTML. */
void owf_buf_put_xml(owf_buf *buf, const char *utf8, size_t length);
void owf_buf_puts_xml(owf_buf *buf, const char *utf8);
/* Removes spaces and tabs at start (an offset into the buffer). */
void owf_buf_trim_from(owf_buf *buf, size_t start);
/* Hands the contents over (NUL-terminated); the buffer is empty after. */
int owf_buf_take(owf_buf *buf, unsigned char **data, size_t *length);

/* ---- Character sets ---- */

/* One code point as UTF-8 into buf. */
void owf_buf_put_utf8(owf_buf *buf, unsigned long codepoint);
/* ISO-8859-1 (the Amiga's) bytes as UTF-8. */
void owf_buf_put_latin1(owf_buf *buf, const unsigned char *text, size_t length);
/* Windows-1252 bytes as UTF-8. */
void owf_buf_put_cp1252(owf_buf *buf, const unsigned char *text, size_t length);
/* Decodes one UTF-8 code point at *p (not beyond end) and moves *p on;
 * malformed bytes give U+FFFD. */
unsigned long owf_utf8_next(const unsigned char **p, const unsigned char *end);
/* 1 when the bytes are valid UTF-8. */
int owf_utf8_valid(const unsigned char *text, size_t length);

/* ---- Big-endian numbers (IFF) and little-endian (zip) ---- */

#define OWF_BE16(p) ((unsigned)(((p)[0] << 8) | (p)[1]))
#define OWF_BE32(p) (((unsigned long)(p)[0] << 24) | ((unsigned long)(p)[1] << 16) | \
                     ((unsigned long)(p)[2] << 8) | (unsigned long)(p)[3])
#define OWF_ID(a, b, c, d) (((unsigned long)(a) << 24) | ((unsigned long)(b) << 16) | \
                            ((unsigned long)(c) << 8) | (unsigned long)(d))

/* ---- IFF ---- */

typedef struct {
    unsigned long id;
    const unsigned char *data;
    unsigned long size;
} owf_iff_chunk;

typedef struct {
    const unsigned char *pos, *end;
    int truncated;           /* a chunk ran past the end of its FORM */
} owf_iff_reader;

/* 1 when data starts "FORM" ... type. */
int owf_iff_is_form(const unsigned char *data, size_t length, unsigned long type);
/* Starts reading the chunks inside the FORM at data; 0 if it is not one. */
int owf_iff_open(owf_iff_reader *reader, const unsigned char *data, size_t length, unsigned long type);
/* The next chunk; 0 at the end. A chunk cut short by the end of the file is
 * returned with the bytes there are, and reader->truncated set. */
int owf_iff_next(owf_iff_reader *reader, owf_iff_chunk *chunk);

/* ---- Zip archives (ODT, DOCX) ---- */

typedef struct owf_zip owf_zip;
owf_zip *owf_zip_new(owf_buf *out);
/* compress 0 stores the file as it is (ODT's mimetype must be stored). */
int owf_zip_add(owf_zip *zip, const char *name, const void *data, size_t length, int compress);
/* Writes the central directory and frees the writer. */
int owf_zip_finish(owf_zip *zip);
unsigned long owf_crc32(unsigned long crc, const unsigned char *data, size_t length);

/* ---- XML (owf_xml.c) ---- */

enum { OWF_XML_ELEMENT, OWF_XML_TEXT };

typedef struct {
    const char *ns, *name, *value;
} owf_xml_attribute;

typedef struct owf_xml_node {
    int type;
    const char *ns;          /* the namespace URI, "" for none */
    const char *name;        /* the local name; for text, the text (not NUL-terminated) */
    size_t length;           /* text only */
    owf_xml_attribute *attrs;
    int nattrs;
    struct owf_xml_node *parent, *first, *last, *next;
} owf_xml_node;

typedef struct owf_xml owf_xml;
owf_xml *owf_xml_parse(const char *text, size_t length, int *error);
void owf_xml_free(owf_xml *xml);
owf_xml_node *owf_xml_root(const owf_xml *xml);
int owf_xml_is(const owf_xml_node *node, const char *ns, const char *name);
const char *owf_xml_attr(const owf_xml_node *node, const char *ns, const char *name);
owf_xml_node *owf_xml_child(const owf_xml_node *node, const char *ns, const char *name);
owf_xml_node *owf_xml_next(const owf_xml_node *node, const char *ns, const char *name);
/* "2.5cm", "12pt", "0.5in"... as twips; 0 if it is not a length. */
int owf_parse_length(const char *s, int *twips);
/* "#RRGGBB" or "RRGGBB"; 0 if it is not one. */
int owf_parse_colour(const char *s, unsigned long *colour);

/* ---- Zip reading (owf_unzip.c) ---- */

typedef struct {
    const unsigned char *data;
    size_t length;
    const unsigned char *dir;     /* the central directory */
    unsigned entries;
} owf_unzip;

/* 0 when the data is not a zip we can read. */
int owf_unzip_open(owf_unzip *zip, const unsigned char *data, size_t length);
int owf_unzip_has(const owf_unzip *zip, const char *name);
/* Extracts one file into memory from malloc (NUL-terminated). */
int owf_unzip_read(const owf_unzip *zip, const char *name, unsigned char **data, size_t *length);

/* ---- Named styles (owf_style.c), for ODT and DOCX ---- */

enum {
    OWF_CP_FONT = 1 << 0, OWF_CP_SIZE = 1 << 1, OWF_CP_BOLD = 1 << 2, OWF_CP_ITALIC = 1 << 3,
    OWF_CP_UNDERLINE = 1 << 4, OWF_CP_STRIKE = 1 << 5, OWF_CP_POSITION = 1 << 6, OWF_CP_COLOUR = 1 << 7
};
enum {
    OWF_PP_ALIGN = 1 << 0, OWF_PP_LEFT = 1 << 1, OWF_PP_RIGHT = 1 << 2, OWF_PP_FIRST = 1 << 3,
    OWF_PP_BEFORE = 1 << 4, OWF_PP_AFTER = 1 << 5, OWF_PP_LINE = 1 << 6, OWF_PP_BREAK = 1 << 7,
    OWF_PP_TABS = 1 << 8, OWF_PP_HEADING = 1 << 9
};
enum { OWF_FAMILY_PARAGRAPH = 1, OWF_FAMILY_TEXT, OWF_FAMILY_LIST };

typedef struct { unsigned mask; owf_charfmt fmt; } owf_charprops;
typedef struct { unsigned mask; owf_parafmt fmt; } owf_paraprops;

typedef struct {
    const char *name;
    int family;
    const char *parent;
    owf_paraprops pp;
    owf_charprops cp;
    const char *list;        /* a list style the paragraphs take, or NULL */
    int list_level;          /* -1 when the style does not set one */
} owf_style;

typedef struct {
    owf_style *items;
    int count, capacity;
} owf_styles;

void owf_charprops_apply(owf_charfmt *to, const owf_charprops *props);
void owf_paraprops_apply(owf_parafmt *to, const owf_paraprops *props);
owf_style *owf_styles_add(owf_styles *styles, const char *name, int family);
const owf_style *owf_styles_find(const owf_styles *styles, const char *name, int family);
void owf_styles_free(owf_styles *styles);
/* Applies the named style and its ancestors, from the first table that has
 * it (automatic styles before common ones). */
void owf_styles_resolve(const owf_styles *const *tables, int ntables, const char *name, int family,
                        owf_parafmt *para, owf_charfmt *chr);
const owf_style *owf_styles_lookup(const owf_styles *const *tables, int ntables, const char *name, int family);

/* Lists: how each level is numbered. */
typedef enum {
    OWF_NUMBER_BULLET, OWF_NUMBER_DECIMAL, OWF_NUMBER_LOWER_LETTER, OWF_NUMBER_UPPER_LETTER,
    OWF_NUMBER_LOWER_ROMAN, OWF_NUMBER_UPPER_ROMAN, OWF_NUMBER_NONE
} owf_number_format;

#define OWF_LIST_LEVELS 9

typedef struct {
    owf_number_format format;
    char text[24];           /* DOCX's "%1.%2." or ODT's prefix + "%n" + suffix; a bullet's character */
    int start;
    int indent;              /* twips, 0 for the default */
} owf_list_level;

typedef struct {
    const char *name;
    owf_list_level levels[OWF_LIST_LEVELS];
} owf_list_style;

void owf_format_number(owf_buf *out, int n, owf_number_format format);

/* ---- Shared by the importers ---- */

/* The colours of ISO 6429 (and ANSI text), 0 to 7. */
extern const unsigned long owf_iso_colours[8];

/* Formatting state while text goes into a paragraph. */
typedef struct {
    owf_doc *doc;
    owf_story *story;
    owf_para *para;          /* the paragraph being filled, or NULL */
    owf_parafmt parafmt;     /* for the next paragraph */
    owf_charfmt charfmt;
    owf_buf text;            /* text not yet added as a run */
    int failed;
} owf_builder;

void owf_builder_init(owf_builder *b, owf_doc *doc, owf_story *story);
/* Text, a tab, a line break or a field at the current formatting. */
void owf_builder_text(owf_builder *b, const char *utf8, size_t length);
void owf_builder_special(owf_builder *b, owf_run_kind kind, owf_field field);
/* Call before the character formatting changes. */
void owf_builder_flush(owf_builder *b);
/* Ends the paragraph (an empty one is still a paragraph). */
void owf_builder_end_para(owf_builder *b);
/* Finishes: flushes text and frees the builder's buffer. OWF_OK or OWF_ERR_MEMORY. */
int owf_builder_done(owf_builder *b);

/* The Symbol typeface's character c (or Word's U+F0xx form) as Unicode. */
unsigned long owf_symbol_char(unsigned c);

/* Readers that live beside their format's writer. */
int owf_import_odt(const unsigned char *data, size_t length, owf_doc *doc, owf_report *report);
int owf_detect_odt(const unsigned char *data, size_t length);
int owf_import_docx(const unsigned char *data, size_t length, owf_doc *doc, owf_report *report);
int owf_detect_docx(const unsigned char *data, size_t length);

/* The formats. */
extern const owf_format owf_format_fodt, owf_format_wordworth, owf_format_finalwriter;
extern const owf_format owf_format_text, owf_format_amiga_text, owf_format_ftxt,
    owf_format_ansi, owf_format_prowrite, owf_format_html, owf_format_pdf, owf_format_odt, owf_format_docx;

#endif
