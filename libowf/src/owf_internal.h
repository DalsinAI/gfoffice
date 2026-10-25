/*
 * libowf's own helpers: growing buffers, UTF-8 and character sets, IFF
 * chunks, zip writing. Not part of the public interface.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#ifndef OWF_INTERNAL_H
#define OWF_INTERNAL_H

#include "owf.h"

#include <stdarg.h>
#include <stddef.h>

/* ---- The system: files and the clock ----
 * owf_sys.c has them over the C library (the PC, and Amiga programs that
 * link libowf in); library/owf_sys_dos.c over dos.library for owf.library,
 * which has no C library of its own. */

/* Reads a whole file; *data gets a NUL after the bytes (from malloc). */
int owf_sys_read_file(const char *path, unsigned char **data, size_t *length);
/* Writes a whole file, replacing it. */
int owf_sys_write_file(const char *path, const unsigned char *data, size_t length);
typedef struct {
    int year, month, day;        /* 1980..., 1-12, 1-31 */
    int hour, minute, second;
} owf_time;
/* The local time now; 0 when the clock can't be read. */
int owf_sys_now(owf_time *t);

/* ---- ASCII case, as the file formats' names and keywords are ---- */

static inline int owf_lower(int c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; }
static inline int owf_upper(int c) { return c >= 'a' && c <= 'z' ? c - ('a' - 'A') : c; }
/* Two names the same but for case. */
int owf_same_word(const char *a, const char *b);
/* 1 when ext is one of the space-separated words in list. */
int owf_ext_in_list(const char *list, const char *ext);

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

/* ---- OLE compound files (owf_cfb.c): Excel 97-2003, Word 97-2003 ---- */

typedef struct {
    const unsigned char *data;
    size_t length;
    unsigned sector_size;
    unsigned long nsectors;
    unsigned long *fat, nfat;
    unsigned long *minifat, nminifat;
    unsigned char *ministream;
    size_t ministream_length;
    unsigned char *dir;           /* the directory's entries, 128 bytes each */
    size_t ndir;
    int cut_short;                /* a chain ran off the end of the file */
} owf_cfb;

/* 1 when the data starts as a compound file. */
int owf_cfb_detect(const unsigned char *data, size_t length);
/* OWF_OK, or OWF_ERR_FORMAT, _CORRUPT or _MEMORY (then nothing to close). */
int owf_cfb_open(owf_cfb *cfb, const unsigned char *data, size_t length);
void owf_cfb_close(owf_cfb *cfb);
int owf_cfb_has(const owf_cfb *cfb, const char *name);
/* A stream, by name (ignoring case), into memory from malloc with a NUL
 * after it. OWF_ERR_FORMAT when there is none. */
int owf_cfb_read(owf_cfb *cfb, const char *name, unsigned char **data, size_t *length);
/* Entries by number (0 is the root storage) and storages by path: "Quill/
 * QuillSub/CONTENTS", names ignoring case; -1 when there is none. The walk
 * over a storage's children is bounded, so a damaged directory ends it. */
const unsigned char *owf_cfb_entry(const owf_cfb *cfb, unsigned long entry);
int owf_cfb_entry_name(const owf_cfb *cfb, unsigned long entry, char *out, size_t size);
int owf_cfb_children(const owf_cfb *cfb, unsigned long storage,
                     int (*fn)(const owf_cfb *cfb, unsigned long entry, void *user), void *user);
long owf_cfb_find_path(const owf_cfb *cfb, const char *path);
int owf_cfb_read_entry(owf_cfb *cfb, unsigned long entry, unsigned char **data, size_t *length);

/* ---- Workbook formats ---- */

extern const owf_book_format owf_book_format_xls;
extern const owf_book_format owf_book_format_csv;
/* Excel's functions, by their number in its files ([MS-XLS] Ftab): name,
 * and the least and most arguments (min == max for a fixed count); NULL
 * name for a number that is not a function we know. */
typedef struct {
    const char *name;
    signed char min, max;
} owf_xl_function;
const owf_xl_function *owf_xl_function_at(unsigned index);
/* A column's letters ("A", "IV"); out holds at least 8 bytes. */
void owf_column_name(unsigned col, char *out);

/* ---- XLSX, ODS and CSV (in_xlsx.c, out_xlsx.c, in_ods.c, out_ods.c,
 * in_csv.c) and what they share ---- */
extern const owf_book_format owf_book_format_xlsx, owf_book_format_xlsm, owf_book_format_ods,
    owf_book_format_fods;
int owf_export_xlsx(const owf_book *book, unsigned char **data, size_t *length, owf_report *report);
int owf_export_ods(const owf_book *book, unsigned char **data, size_t *length, owf_report *report);
int owf_export_fods(const owf_book *book, unsigned char **data, size_t *length, owf_report *report);
/* CSV's reader (in_csv.c), beside its writer (out_csv.c). */
int owf_import_csv(const unsigned char *data, size_t length, owf_book *book, owf_report *report);
int owf_detect_csv(const unsigned char *data, size_t length);

/* Numbers in files (owf_booknum.c). The whole of s..s+length as a decimal
 * number ("12", "-0.5", "1.5E+20"), the nearest double; 0 when it is not
 * one or is beyond a double. */
int owf_number_parse(const char *s, size_t length, double *value);
/* The fewest digits (up to 17) that read back as the same double: "0.1",
 * "0.30000000000000004". out holds at least 32 bytes. */
void owf_number_exact(double v, char *out);
/* Days since 30 December 1899, and back (proleptic Gregorian). */
long owf_days_from_date(long year, int month, int day);
void owf_date_from_days(long days, long *year, int *month, int *day);
/* ISO 8601 as ODF writes it: "2026-10-21", "2026-10-21T13:45:30.5" as a
 * day number; "PT13H45M30S" as days. 0 when it is not one. */
int owf_parse_iso_date(const char *s, double *serial);
int owf_parse_iso_duration(const char *s, double *days);
int owf_put_iso_date(owf_buf *out, double serial, int with_time);
int owf_put_iso_duration(owf_buf *out, double days);
/* What a number format shows: by its first section. */
enum { OWF_NUMFMT_NUMBER, OWF_NUMFMT_DATE, OWF_NUMFMT_TIME, OWF_NUMFMT_PERCENT, OWF_NUMFMT_TEXT };
int owf_numfmt_kind(const char *format);

/* Formulas in files (owf_xlformula.c); each gives text from malloc, or
 * NULL when memory runs out (or, from OpenFormula, the formula cannot be
 * read). */
/* Relative references moved by drow and dcol, as XLSX's shared formulas
 * need; one moved off the sheet becomes #REF!. */
char *owf_xl_formula_move(const char *text, long drow, long dcol);
/* From an XLSX file: "_xlfn." (and "_xlws.", "_xlpm.") off, and another
 * book's number ("[1]Rates!A1") as its file's name, books[0] for [1]. */
char *owf_xl_formula_from_file(const char *text, char *const *books, int nbooks);
/* For an XLSX file: "_xlfn." on where Excel wants it, and with links, each
 * other book by its number, the books, their sheets and names collected in
 * links for the file's externalLink parts. */
typedef struct {
    char *file;
    char **sheets;
    int nsheets, capsheets;
    char **names;
    int nnames, capnames;
} owf_xl_link;
typedef struct {
    owf_xl_link *items;
    int count, capacity, failed;
} owf_xl_links;
char *owf_xl_formula_for_file(const char *text, const owf_book *book, owf_xl_links *links);
void owf_xl_links_free(owf_xl_links *links);
/* OpenFormula ("of:=SUM([.A1:.A3])") to Excel's text and back. */
char *owf_of_to_excel(const char *text);
char *owf_excel_to_of(const char *text);
/* The same, *lost set when something had to go: a sheet's or another
 * book's own name ("Sheet1!Rate") becomes the name alone. */
char *owf_excel_to_of_lossy(const char *text, int *lost);
/* Excel's built-in number formats by number (NULL for the country's own)
 * and its 64 colours by number. */
const char *owf_xl_builtin_format(unsigned id);
extern const unsigned long owf_xl_palette[64];
/* "#DIV/0!" for an OWF_ERROR_...; the code of such a name, or -1. */
const char *owf_xl_error_text(int error);
int owf_xl_error_code(const char *s, size_t length);
/* UTF-8 that is valid: bad bytes become U+FFFD. */
void owf_buf_put_utf8_clean(owf_buf *out, const char *s, size_t length);

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

/* Reading as a stream, for parts too big for a tree (a sheet's cells): one
 * start tag, end tag or run of text at a time. text must be writable with
 * a byte after length (owf_unzip_read's has); it is parsed in place and
 * must outlive the scanner. An empty element gives its start, then its end. */
enum { OWF_XS_START = 1, OWF_XS_END, OWF_XS_TEXT };
typedef struct {
    int type;
    const char *ns, *name;           /* START and END */
    const owf_xml_attribute *attrs;  /* START */
    int nattrs, empty;
    const char *text;                /* TEXT: decoded, not NUL-terminated */
    size_t length;
    int depth;                       /* the element's, the root's is 1 */
} owf_xml_event;
typedef struct owf_xml_scan owf_xml_scan;
owf_xml_scan *owf_xml_scan_new(char *text, size_t length);
void owf_xml_scan_free(owf_xml_scan *scan);
/* 1 with the next event, 0 after the root element's end, -1 when the XML
 * is damaged or memory ran out (owf_xml_scan_error says which). */
int owf_xml_scan_next(owf_xml_scan *scan, owf_xml_event *event);
int owf_xml_scan_error(const owf_xml_scan *scan);
const char *owf_xml_event_attr(const owf_xml_event *event, const char *ns, const char *name);

/* ---- Zip reading (owf_unzip.c) ---- */

/* owf_unzip, owf_unzip_open, owf_unzip_has and owf_unzip_read are public
 * since owf.library 2: owf_zip.h. */
#include "owf_zip.h"
/* What an ODF package (by its mimetype) or an OOXML package (by its
 * content types) holds, so that each reader claims only its own. */
enum {
    OWF_PACKAGE_UNKNOWN, OWF_PACKAGE_TEXT, OWF_PACKAGE_SHEET, OWF_PACKAGE_SHEET_MACROS, OWF_PACKAGE_SHEET_BINARY,
    OWF_PACKAGE_SLIDES, OWF_PACKAGE_OTHER
};
int owf_odf_kind(const unsigned char *data, size_t length);
int owf_ooxml_kind(const unsigned char *data, size_t length);
/* A relationship's target relative to dir (or from the package's root
 * when it starts with "/"), with "." and ".." worked out:
 * "xl/worksheets/sheet1.xml". 0 when it does not fit. */
int owf_opc_part_name(char *out, size_t size, const char *dir, const char *target);

/* ---- Named styles (owf_style.c), for ODT and DOCX ---- */

enum {
    OWF_CP_FONT = 1 << 0, OWF_CP_SIZE = 1 << 1, OWF_CP_BOLD = 1 << 2, OWF_CP_ITALIC = 1 << 3,
    OWF_CP_UNDERLINE = 1 << 4, OWF_CP_STRIKE = 1 << 5, OWF_CP_POSITION = 1 << 6, OWF_CP_COLOUR = 1 << 7
};
enum {
    OWF_PP_ALIGN = 1 << 0, OWF_PP_LEFT = 1 << 1, OWF_PP_RIGHT = 1 << 2, OWF_PP_FIRST = 1 << 3,
    OWF_PP_BEFORE = 1 << 4, OWF_PP_AFTER = 1 << 5, OWF_PP_LINE = 1 << 6, OWF_PP_BREAK = 1 << 7,
    OWF_PP_TABS = 1 << 8, OWF_PP_HEADING = 1 << 9, OWF_PP_SHADING = 1 << 10, OWF_PP_BORDERS = 1 << 11,
    /* with OWF_PP_TABS: the stops go in among the ones there (Word's), a stop of kind OWF_TAB_CLEARED taking
     * the one at its place away; without it they replace them (ODF's) */
    OWF_PP_TABS_MERGE = 1 << 12
};
/* A tab stop that takes away the one at its place (Word's w:val="clear"): in an owf_paraprops only, never in
 * a document. */
#define OWF_TAB_CLEARED ((owf_tab_kind)-1)
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
    int first;               /* the first line from indent, below 0 hangs; 0 for none */
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
void owf_builder_link_text(owf_builder *b, const char *utf8, size_t length, const char *href);
void owf_builder_special(owf_builder *b, owf_run_kind kind, owf_field field);
void owf_builder_image(owf_builder *b, int image_index);
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

/* ODF text as ODT reads it, for the other ODF readers (in_odt.c; ODP's
 * text boxes): fonts, paragraph and text styles, and one paragraph at a
 * time into a builder. Style tables: styles.xml's common styles, its
 * automatic ones (master pages), content.xml's automatic ones. */
enum { OWF_ODF_COMMON, OWF_ODF_STYLES_AUTO, OWF_ODF_CONTENT_AUTO };
typedef struct owf_odf_text owf_odf_text;
owf_odf_text *owf_odf_text_new(owf_doc *doc, owf_report *report, const owf_unzip *zip);
void owf_odf_text_free(owf_odf_text *t);
void owf_odf_text_faces(owf_odf_text *t, const owf_xml_node *decls);
void owf_odf_text_styles(owf_odf_text *t, const owf_xml_node *container, int which);
void owf_odf_text_props(owf_odf_text *t, const owf_xml_node *n, owf_charprops *cp);
void owf_odf_para_props(const owf_xml_node *n, owf_paraprops *pp);
/* A tab stop's leader as ODF's style:leader-style and style:leader-text attributes (out_odt.c) */
void owf_odf_put_leader(owf_buf *out, owf_tab_leader leader);
/* A text:p or text:h: base_para and base_char, then its styles; runs keep
 * font -1 and size 0 where they are base_char's. */
int owf_odf_text_paragraph(owf_odf_text *t, owf_builder *b, const owf_xml_node *p, int which,
                           const owf_parafmt *base_para, const owf_charfmt *base_char);

/* The page PDF writer's pieces (owf_pdf.h): a picture as a PDF image
 * (out_pdf.c's JPEG and PNG code; JPEG kept as it is, PNG decoded) and the
 * standard fonts' widths (owf_pdffonts.c). */
typedef struct {
    int w, h, gray, dct;
    unsigned char *data;
    size_t len;
    unsigned char *mask;         /* deflated alpha, or NULL */
    size_t mlen;
    int owned;                   /* data is the picture's own copy */
} owf_pdf_picture;
int owf_pdf_encode_picture(const owf_image *image, owf_pdf_picture *out);
void owf_pdf_picture_free(owf_pdf_picture *p);
/* Helvetica, Helvetica-Bold, -Oblique, -BoldOblique, then Times' four, in
 * thousandths of an em, for WinAnsiEncoding's bytes 32 to 255. */
extern const unsigned short owf_pdf_widths[8][224];

/* The formats. */
extern const owf_format owf_format_fodt, owf_format_wordworth, owf_format_finalwriter;
extern const owf_format owf_format_text, owf_format_amiga_text, owf_format_ftxt,
    owf_format_ansi, owf_format_prowrite, owf_format_html, owf_format_pdf, owf_format_odt, owf_format_docx;

#endif
