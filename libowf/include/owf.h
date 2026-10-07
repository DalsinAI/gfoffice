/*
 * libowf: OpenWrite's file formats. One document model; importers build it,
 * exporters write it. Plain C99, zlib optional; runs on any Amiga and on the
 * PC, where the tests run.
 * MIT, Copyright (c) 2026 Dalsin Limited.
 */
#ifndef OWF_H
#define OWF_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OWF_VERSION "0.1"

/* Results. */
enum {
    OWF_OK = 0,
    OWF_ERR_IO,          /* the file could not be read or written */
    OWF_ERR_FORMAT,      /* not a format we know, or not the one asked for */
    OWF_ERR_CORRUPT,     /* the format is right but the file is damaged */
    OWF_ERR_MEMORY,
    OWF_ERR_UNSUPPORTED  /* the format cannot be read (or written) */
};
const char *owf_error_text(int error);

/* Lengths are in twips (1/20 point, 1/1440 inch) throughout. */
#define OWF_TWIPS_PER_POINT 20
#define OWF_TWIPS_PER_INCH 1440

/* ---- Character formatting ---- */

enum {
    OWF_BOLD = 1 << 0,
    OWF_ITALIC = 1 << 1,
    OWF_UNDERLINE = 1 << 2,
    OWF_STRIKE = 1 << 3,
    OWF_SUPER = 1 << 4,
    OWF_SUB = 1 << 5
};

#define OWF_COLOUR_AUTO 0xFFFFFFFFUL

typedef struct {
    unsigned flags;          /* OWF_BOLD ... */
    int font;                /* index into the document's fonts; -1: the default */
    int size;                /* twips; 0: the default */
    unsigned long colour;    /* 0xRRGGBB, or OWF_COLOUR_AUTO */
} owf_charfmt;

/* ---- Paragraphs ---- */

typedef enum { OWF_RUN_TEXT, OWF_RUN_TAB, OWF_RUN_LINEBREAK, OWF_RUN_FIELD, OWF_RUN_IMAGE } owf_run_kind;
typedef enum { OWF_FIELD_PAGE, OWF_FIELD_PAGES, OWF_FIELD_DATE, OWF_FIELD_TIME } owf_field;

typedef struct {
    owf_run_kind kind;
    owf_charfmt fmt;
    char *text;              /* UTF-8, for OWF_RUN_TEXT */
    char *href;              /* optional hyperlink target for text */
    owf_field field;         /* for OWF_RUN_FIELD */
    int image;               /* index into doc->images for OWF_RUN_IMAGE */
} owf_run;

typedef enum { OWF_ALIGN_LEFT, OWF_ALIGN_CENTRE, OWF_ALIGN_RIGHT, OWF_ALIGN_JUSTIFY } owf_align;
typedef enum { OWF_TAB_LEFT, OWF_TAB_CENTRE, OWF_TAB_RIGHT, OWF_TAB_DECIMAL } owf_tab_kind;

typedef struct {
    int position;            /* twips from the paragraph's left indent */
    owf_tab_kind kind;
} owf_tab;

#define OWF_MAX_TABS 32

typedef struct {
    int heading;             /* 0 for body text, 1 to 6 for headings */
    owf_align align;
    int indent_left, indent_right;
    int indent_first;        /* the first line, relative to indent_left */
    int space_before, space_after;
    int line_spacing;        /* percent: 100 single, 200 double */
    int page_break_before;
    int ntabs;
    owf_tab tabs[OWF_MAX_TABS];
} owf_parafmt;

typedef struct {
    owf_parafmt fmt;
    int nruns, capruns;
    owf_run *runs;
    /* Native table cells are ordinary editable paragraphs tagged with grid
     * coordinates. table_id < 0 means a normal paragraph. */
    int table_id, table_row, table_col, table_cols;
} owf_para;

/* A run of paragraphs: the body, a header or a footer. */
typedef struct {
    int nparas, capparas;
    owf_para *paras;
} owf_story;

/* ---- Fonts and the page ---- */

typedef enum { OWF_FONT_ANY, OWF_FONT_SERIF, OWF_FONT_SANS, OWF_FONT_MONO } owf_font_kind;

typedef struct {
    char *name;              /* as the document names it */
    owf_font_kind kind;
} owf_font;

typedef enum {
    OWF_PAGENUM_ARABIC, OWF_PAGENUM_ROMAN_UPPER, OWF_PAGENUM_ROMAN_LOWER,
    OWF_PAGENUM_LETTER_UPPER, OWF_PAGENUM_LETTER_LOWER
} owf_pagenum_style;

typedef struct {
    int width, height;
    int margin_top, margin_right, margin_bottom, margin_left;
    int start_page;
    owf_pagenum_style pagenum_style;
    int header_on_first, footer_on_first;
} owf_page;

typedef struct {
    char *name, *mime, *alt;
    unsigned char *data;
    size_t length;
    int width, height;       /* display size in twips */
} owf_image;

typedef struct owf_doc {
    owf_story body, header, footer;
    int nfonts, capfonts;
    owf_font *fonts;
    int nimages, capimages;
    owf_image *images;
    owf_charfmt base;        /* the document's default font and size */
    owf_page page;
    char *title;             /* UTF-8, may be NULL */
} owf_doc;

owf_doc *owf_doc_new(void);
void owf_doc_free(owf_doc *doc);

/* Building a document (importers). They return NULL or -1 when memory runs
 * out; the document stays valid. */
void owf_parafmt_init(owf_parafmt *fmt);
void owf_charfmt_init(owf_charfmt *fmt);
owf_para *owf_story_add(owf_story *story, const owf_parafmt *fmt);
int owf_para_add_text(owf_para *para, const owf_charfmt *fmt, const char *utf8, size_t len);
int owf_para_add_link_text(owf_para *para, const owf_charfmt *fmt, const char *utf8, size_t len, const char *href);
int owf_para_add_special(owf_para *para, const owf_charfmt *fmt, owf_run_kind kind, owf_field field);
int owf_para_add_image(owf_para *para, const owf_charfmt *fmt, int image_index);
int owf_doc_add_image(owf_doc *doc, const char *name, const char *mime, const void *data, size_t length, int width, int height, const char *alt);
int owf_doc_font(owf_doc *doc, const char *name, owf_font_kind kind);
int owf_doc_set_title(owf_doc *doc, const char *utf8);

/* Today's equivalent of an Amiga font name ("times" gives "Liberation
 * Serif"), or NULL when the name is not one we map. */
const char *owf_font_modern(const char *name, owf_font_kind *kind);

/* ---- The import report ---- */

typedef enum { OWF_NOTE_INFO, OWF_NOTE_APPROX, OWF_NOTE_LOST } owf_note_level;
typedef struct owf_report owf_report;

owf_report *owf_report_new(void);
void owf_report_free(owf_report *report);
/* Adds a note; the same note twice is counted, not repeated. A NULL report
 * is allowed and ignored. */
void owf_report_add(owf_report *report, owf_note_level level, const char *fmt, ...);
int owf_report_count(const owf_report *report);
/* The n-th note, its level and how many times it happened. */
const char *owf_report_note(const owf_report *report, int n, owf_note_level *level, int *times);

/* ---- Formats ---- */

typedef struct owf_format {
    const char *name;            /* "odt", "prowrite"... */
    const char *description;     /* "OpenDocument Text" */
    const char *extensions;      /* "odt ott", without dots */
    /* How sure we are that this is the format: 0 (no) to 100. */
    int (*detect)(const unsigned char *data, size_t length);
    int (*import)(const unsigned char *data, size_t length, owf_doc *doc, owf_report *report);
    /* Writes the whole file into memory: *data from malloc(). */
    int (*export)(const owf_doc *doc, unsigned char **data, size_t *length, owf_report *report);
} owf_format;

int owf_format_count(void);
const owf_format *owf_format_at(int index);
const owf_format *owf_format_named(const char *name);
/* The writable format for a file name's extension, or NULL. */
const owf_format *owf_format_for_filename(const char *filename);

/* Reads a document; format NULL means find it from the contents. *used, if
 * not NULL, is set to the format that read it. */
int owf_import_memory(const unsigned char *data, size_t length, const char *format,
                      owf_doc **doc, owf_report *report, const owf_format **used);
int owf_import_file(const char *path, const char *format, owf_doc **doc,
                    owf_report *report, const owf_format **used);
/* Writes a document; format NULL means by the file name's extension. */
int owf_export_file(const owf_doc *doc, const char *path, const char *format, owf_report *report);

#ifdef __cplusplus
}
#endif

#endif
