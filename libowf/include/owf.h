/*
 * libowf: OpenWrite's file formats. One document model; importers build it,
 * exporters write it. Plain C99, zlib optional; runs on any Amiga and on the
 * PC, where the tests run.
 *
 * On the Amiga it is also owf.library, shared by every program that reads
 * or writes documents: define OWF_USE_LIBRARY, open "owf.library" into
 * OWFBase (owf_open.h does both), and the calls below go to the library.
 * The types are the same either way.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#ifndef OWF_H
#define OWF_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OWF_VERSION "1.4.0"

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

typedef enum { OWF_RUN_TEXT, OWF_RUN_TAB, OWF_RUN_LINEBREAK, OWF_RUN_FIELD, OWF_RUN_IMAGE,
               OWF_RUN_FRAME   /* 1.3: a frame anchored here (its image field is the frame's index) */
} owf_run_kind;
typedef enum { OWF_FIELD_PAGE, OWF_FIELD_PAGES, OWF_FIELD_DATE, OWF_FIELD_TIME } owf_field;

typedef struct {
    owf_run_kind kind;
    owf_charfmt fmt;
    char *text;              /* UTF-8, for OWF_RUN_TEXT */
    char *href;              /* optional hyperlink target for text */
    owf_field field;         /* for OWF_RUN_FIELD */
    int image;               /* index into doc->images for OWF_RUN_IMAGE, doc->frames for OWF_RUN_FRAME */
} owf_run;

typedef enum { OWF_ALIGN_LEFT, OWF_ALIGN_CENTRE, OWF_ALIGN_RIGHT, OWF_ALIGN_JUSTIFY } owf_align;
typedef enum { OWF_TAB_LEFT, OWF_TAB_CENTRE, OWF_TAB_RIGHT, OWF_TAB_DECIMAL } owf_tab_kind;
/* 1.2: what fills a tab's space (a contents page's dots) */
typedef enum {
    OWF_LEADER_NONE, OWF_LEADER_DOT, OWF_LEADER_HYPHEN, OWF_LEADER_UNDERSCORE, OWF_LEADER_MIDDLE_DOT,
    OWF_LEADER_HEAVY
} owf_tab_leader;

typedef struct {
    int position;            /* twips from the paragraph's left indent */
    owf_tab_kind kind;
    owf_tab_leader leader;   /* 1.2 */
} owf_tab;

#define OWF_MAX_TABS 32

typedef struct {
    int heading;             /* 0 for body text, 1 to 6 for headings */
    owf_align align;
    int indent_left, indent_right;
    int indent_first;        /* the first line, relative to indent_left */
    int space_before, space_after;
    int line_spacing;        /* percent: 100 single, 200 double; 2.0.18: or a height (OWF_LINE_EXACT, _AT_LEAST) */
    int page_break_before;
    int ntabs;
    owf_tab tabs[OWF_MAX_TABS];
    unsigned long shading;   /* OWF_SHADE(0xRRGGBB) behind the paragraph; 0: none */
    int borders;             /* OWF_BORDER_TOP | _LEFT | _BOTTOM | _RIGHT; 0: none */
    unsigned long border_colour; /* OWF_SHADE(0xRRGGBB); 0: automatic (black) */
} owf_parafmt;

/* 2.0.18: line spacing as a height, not a percent (Word's lineRule exact and atLeast, ODF's fo:line-height as a
 * length and style:line-height-at-least), kept in line_spacing below 0: OWF_LINE_EXACT(twips) every line exactly
 * that high, OWF_LINE_AT_LEAST(twips) every line at least that high. A writer that knows percents alone takes
 * OWF_LINE_PERCENT (100 for a height). */
#define OWF_LINE_EXACT(twips)    (-(int)(twips))
#define OWF_LINE_AT_LEAST(twips) (-(int)(twips) - 0x8000)
#define OWF_LINE_IS_HEIGHT(v)    ((v) < 0)
#define OWF_LINE_IS_EXACT(v)     ((v) < 0 && (v) > -0x8000)
#define OWF_LINE_HEIGHT(v)       ((v) <= -0x8000 ? -(v) - 0x8000 : -(v))
#define OWF_LINE_PERCENT(v)      ((v) > 0 ? (v) : 100)

/* A colour that is set, black included: zero means none or automatic. */
#define OWF_SHADE(rgb) (0x1000000UL | ((unsigned long)(rgb) & 0xFFFFFFUL))
#define OWF_SHADE_SET(v) (((v) & 0x1000000UL) != 0)
#define OWF_SHADE_RGB(v) ((v) & 0xFFFFFFUL)
#define OWF_BORDER_TOP 1
#define OWF_BORDER_LEFT 2
#define OWF_BORDER_BOTTOM 4
#define OWF_BORDER_RIGHT 8

typedef struct {
    owf_parafmt fmt;
    int nruns, capruns;
    owf_run *runs;
    /* Native table cells are ordinary editable paragraphs tagged with grid
     * coordinates. table_id < 0 means a normal paragraph. */
    int table_id, table_row, table_col, table_cols;
    unsigned long cell_shading;  /* a table cell's OWF_SHADE(0xRRGGBB); 0: none */
    /* 1.4: the paragraph's style as its file names it (DOCX's w:pStyle id, "TOC1"), NULL: none. Its format is
     * in fmt already; a heading is written as its level's style whatever this says. */
    char *style;
    /* 1.4: the bookmarks at its start, their names between single spaces ("_Toc123 _Ref7"), NULL: none.
     * DOCX's w:bookmarkStart and ODF's text:bookmark; a link into the document ("#_Toc123") goes to one. */
    char *bookmarks;
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

/* ---- Frames (1.3) ----
 * A picture or a text box out of the text's flow (Word's anchored drawings,
 * wp:anchor): at a place on the page measured from the page, its margins or
 * the paragraph it belongs to, behind the text or in front of it. Its
 * paragraph holds an OWF_RUN_FRAME run, so the frame moves with it. */
typedef enum {
    OWF_FROM_PAGE,           /* the page's edge */
    OWF_FROM_MARGIN,         /* the page's margins (Word's column too) */
    OWF_FROM_PARAGRAPH,      /* the paragraph's top (down), its left indent (across) */
    OWF_FROM_LEFT_MARGIN, OWF_FROM_RIGHT_MARGIN,     /* the margins' own areas, across */
    OWF_FROM_TOP_MARGIN, OWF_FROM_BOTTOM_MARGIN      /* and down */
} owf_frame_from;
typedef enum {
    OWF_PLACE_AT,            /* x, y from the corner */
    OWF_PLACE_START,         /* left or top */
    OWF_PLACE_CENTRE,
    OWF_PLACE_END            /* right or bottom */
} owf_frame_place;
typedef enum {
    OWF_WRAP_NONE, OWF_WRAP_SQUARE, OWF_WRAP_TIGHT, OWF_WRAP_TOP_BOTTOM
} owf_frame_wrap;

typedef struct {
    int image;               /* its picture: into doc->images; -1: none */
    owf_story story;         /* a text box's paragraphs (nparas 0: none) */
    int width, height;       /* twips */
    int x, y;                /* twips from x_from's and y_from's corner, when placed OWF_PLACE_AT */
    unsigned char x_from, y_from;    /* owf_frame_from */
    unsigned char x_place, y_place;  /* owf_frame_place */
    unsigned char behind;    /* behind the text, else in front of it */
    unsigned char wrap;      /* owf_frame_wrap: how text goes round it */
    unsigned char in_line;   /* 2.0.17: a text box in the line (Word's wp:inline), not placed on the page: placed
                                from its paragraph at 0, 0, with no wrap; OpenWrite puts it where its run is */
    unsigned char reserved[1];
    int inset[4];            /* a text box's inner margins, twips: top, right, bottom, left */
    unsigned long fill;      /* OWF_SHADE(0xRRGGBB) inside it; 0: none */
    unsigned long line;      /* OWF_SHADE(0xRRGGBB) round it; 0: none */
} owf_frame;

typedef struct owf_doc {
    owf_story body, header, footer;
    int nfonts, capfonts;
    owf_font *fonts;
    int nimages, capimages;
    owf_image *images;
    owf_charfmt base;        /* the document's default font and size */
    owf_page page;
    char *title;             /* UTF-8, may be NULL */
    int nframes, capframes;  /* 1.3 */
    owf_frame *frames;
} owf_doc;

/* ---- Memory ----
 * Everything an owf_doc holds (stories, paragraphs, runs, their text,
 * fonts, images, the title) comes from these, and a program that changes a
 * document's arrays itself (OpenWrite's editor) uses them too, so that
 * owf.library and the program agree on whose memory it is. Linked in, they
 * are malloc, calloc, realloc and free. */
void *owf_malloc(size_t n);
void *owf_calloc(size_t n, size_t size);
void *owf_realloc(void *p, size_t n);
void owf_free(void *p);

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
/* 1.3: a frame, its index (-1: no memory). The document takes frame->story's paragraphs. */
int owf_doc_add_frame(owf_doc *doc, const owf_frame *frame);
int owf_para_add_frame(owf_para *para, const owf_charfmt *fmt, int frame_index);
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

/* ---- Workbooks: the spreadsheet's model ----
 * A book holds sheets of cells. Cells hold a value (a formula's last
 * result) and, for a formula, its text in Excel's A1 syntax without the "=";
 * their look is a style from the book's list. Numbers are IEEE doubles, as
 * in the files; dates and times are numbers too, days since the book's
 * epoch (30 December 1899, or 1 January 1904 when date1904 is set). */

typedef enum { OWF_VALUE_EMPTY, OWF_VALUE_NUMBER, OWF_VALUE_TEXT, OWF_VALUE_BOOL, OWF_VALUE_ERROR } owf_value_type;

/* Errors, numbered as Excel's files number them. */
enum {
    OWF_ERROR_NULL = 0x00, OWF_ERROR_DIV0 = 0x07, OWF_ERROR_VALUE = 0x0F, OWF_ERROR_REF = 0x17,
    OWF_ERROR_NAME = 0x1D, OWF_ERROR_NUM = 0x24, OWF_ERROR_NA = 0x2A
};
/* "#DIV/0!" and so on; NULL for a number that is not one. */
const char *owf_error_name(int error);

#define OWF_CELL_ARRAY 1         /* part of an array formula's range */

typedef struct {
    unsigned row, col;           /* from 0: A1 is row 0, column 0 */
    owf_value_type type;
    double number;               /* NUMBER; BOOL: 0 or 1 */
    int error;                   /* ERROR: OWF_ERROR_... */
    char *text;                  /* TEXT: UTF-8 */
    char *formula;               /* "SUM(A1:A3)", or NULL */
    int style;                   /* into the book's styles; 0 is the default */
    unsigned flags;              /* OWF_CELL_... */
} owf_cell;

/* Horizontal and vertical alignment. */
enum {
    OWF_HALIGN_GENERAL, OWF_HALIGN_LEFT, OWF_HALIGN_CENTRE, OWF_HALIGN_RIGHT, OWF_HALIGN_FILL,
    OWF_HALIGN_JUSTIFY, OWF_HALIGN_CENTRE_ACROSS
};
enum { OWF_VALIGN_TOP, OWF_VALIGN_CENTRE, OWF_VALIGN_BOTTOM };
/* Border lines, numbered as Excel's files number them. */
enum {
    OWF_LINE_NONE, OWF_LINE_THIN, OWF_LINE_MEDIUM, OWF_LINE_DASHED, OWF_LINE_DOTTED, OWF_LINE_THICK,
    OWF_LINE_DOUBLE, OWF_LINE_HAIR, OWF_LINE_MEDIUM_DASHED, OWF_LINE_DASH_DOT, OWF_LINE_MEDIUM_DASH_DOT,
    OWF_LINE_DASH_DOT_DOT, OWF_LINE_MEDIUM_DASH_DOT_DOT, OWF_LINE_SLANTED_DASH_DOT
};
enum { OWF_SIDE_LEFT, OWF_SIDE_RIGHT, OWF_SIDE_TOP, OWF_SIDE_BOTTOM };

typedef struct {
    owf_charfmt font;            /* font is into the book's fonts */
    int number_format;           /* into the book's number formats; 0 is "General" */
    unsigned char halign, valign, wrap, indent;
    unsigned long fill;          /* OWF_SHADE(0xRRGGBB) behind the cell; 0: none */
    unsigned char border[4];     /* OWF_LINE_... by OWF_SIDE_... */
    unsigned long border_colour[4]; /* OWF_SHADE(0xRRGGBB); 0: automatic */
} owf_cellstyle;

typedef struct {
    unsigned first, last;        /* columns */
    int width;                   /* twips */
    int style;
    int hidden;
} owf_colinfo;

typedef struct {
    unsigned row;
    int height;                  /* twips */
    int hidden;
} owf_rowinfo;

typedef struct {
    unsigned first_row, last_row, first_col, last_col;
} owf_range;

/* Conditional formatting: a rule over ranges of a sheet, as Excel's
 * cfRule. CELL_IS compares each cell's value with formula[0] (and [1]);
 * EXPRESSION holds where formula[0] is true; COLOUR_SCALE colours each
 * number between its points; KEPT is one the model doesn't work out (data
 * bars, icon sets, top 10, above average, duplicates...), kept as its
 * file wrote it (xml) for writing back, and shown by formula[0] like an
 * EXPRESSION when the file gave one (containsText and its kind do). The
 * formulas are as at the first range's top left cell, without "=", and
 * move with each cell as relative references do. A rule that holds gives
 * its cells what its look sets (font_set says which of OWF_BOLD... it
 * sets, font_flags to what); rules go by priority, 1 first, and the first
 * to set a part of the look wins it; stop leaves the later ones out. */
enum { OWF_CF_CELL_IS, OWF_CF_EXPRESSION, OWF_CF_COLOUR_SCALE, OWF_CF_KEPT };
enum {
    OWF_CF_BETWEEN, OWF_CF_NOT_BETWEEN, OWF_CF_EQUAL, OWF_CF_NOT_EQUAL, OWF_CF_GREATER, OWF_CF_LESS,
    OWF_CF_GREATER_EQUAL, OWF_CF_LESS_EQUAL
};
/* A colour scale's point: the lowest or highest number, a number, a
 * percent of the way, a percentile, or a formula's value. */
enum { OWF_CFV_MIN, OWF_CFV_MAX, OWF_CFV_NUMBER, OWF_CFV_PERCENT, OWF_CFV_PERCENTILE, OWF_CFV_FORMULA };

typedef struct {
    int kind;                    /* OWF_CFV_... */
    char *value;                 /* NUMBER, PERCENT, PERCENTILE: the number as text; FORMULA: the formula; else NULL */
    unsigned long colour;        /* OWF_SHADE(0xRRGGBB) */
} owf_cfpoint;

typedef struct {
    int nranges;
    owf_range *ranges;           /* where it applies */
    int type;                    /* OWF_CF_... */
    int op;                      /* CELL_IS: OWF_CF_BETWEEN... */
    int priority;                /* 1 first */
    int stop;                    /* stopIfTrue */
    char *formula[2];            /* or NULL */
    unsigned font_set, font_flags;
    unsigned long font_colour;   /* OWF_SHADE(0xRRGGBB), or 0: not set */
    unsigned long fill;          /* OWF_SHADE(0xRRGGBB), or 0: not set */
    int npoints;                 /* COLOUR_SCALE: 2 or 3 */
    owf_cfpoint points[3];
    char *xml;                   /* KEPT: its cfRule element as the file had it, but its look's dxfId */
} owf_condfmt;

/* Data validation over ranges, as Excel's dataValidation: what a cell
 * may hold (a whole number, a decimal, one of a list, a date, a time, a
 * text of a length, or what a formula allows), compared by op with
 * formula[0] (and [1]) as at the first range's top left cell; a list's
 * formula[0] is "\"Yes,No\"" or a range. The messages are UTF-8 or NULL. */
enum {
    OWF_DV_ANY, OWF_DV_WHOLE, OWF_DV_DECIMAL, OWF_DV_LIST, OWF_DV_DATE, OWF_DV_TIME, OWF_DV_TEXT_LENGTH,
    OWF_DV_CUSTOM
};
enum { OWF_DV_STOP, OWF_DV_WARNING, OWF_DV_INFORMATION };

typedef struct {
    int nranges;
    owf_range *ranges;
    int type;                    /* OWF_DV_... */
    int op;                      /* OWF_CF_BETWEEN...: as conditional formatting's cellIs */
    int allow_blank;
    int no_dropdown;             /* a list without its arrow (Excel's showDropDown) */
    int show_prompt, show_error;
    int error_style;             /* OWF_DV_STOP... */
    char *formula[2];
    char *prompt_title, *prompt, *error_title, *error;
} owf_validation;

/* A comment (Excel's note) on a cell: its author and text, UTF-8, from
 * malloc; the author may be NULL. */
typedef struct {
    unsigned row, col;
    char *author;
    char *text;
} owf_comment;

typedef struct {
    char *name;                  /* UTF-8 */
    int ncells, capcells;
    owf_cell *cells;             /* by row, then column */
    int ncols, capcols;
    owf_colinfo *cols;
    int nrows, caprows;
    owf_rowinfo *rows;           /* by row */
    int nmerges, capmerges;
    owf_range *merges;
    int default_width;           /* a column's, twips */
    int default_height;          /* a row's, twips */
    int hidden;                  /* 1 hidden, 2 hidden from the user too */
    unsigned freeze_rows, freeze_cols;
    int ncondfmts, capcondfmts;  /* owf.library 2.1 */
    owf_condfmt *condfmts;
    int ncomments, capcomments;
    owf_comment *comments;       /* by row, then column */
    int nvalidations, capvalidations;
    owf_validation *validations;
    int has_autofilter;          /* an AutoFilter: its buttons on autofilter's first row */
    owf_range autofilter;
    char *autofilter_xml;        /* what it filters and sorts by, as the file wrote it (Excel's filterColumn and
                                    sortState elements), or NULL */
    char *drawing;               /* its drawing (charts, pictures) in the book's kept parts: the part's name, or NULL */
} owf_sheet;

/* A defined name: "Prices" standing for "Sheet1!$B$2:$B$40". */
typedef struct {
    char *name;                  /* UTF-8 */
    int sheet;                   /* the sheet it belongs to; -1: the whole book */
    char *formula;               /* what it stands for, as a formula without "=" */
    int hidden;
} owf_defname;

/* A part of a file kept as it was, for writing it back: an .xlsx sheet's
 * drawing and everything it shows (its relationships, charts, their
 * styles and colours, pictures), by their names in the package. */
typedef struct {
    char *name;                  /* "xl/charts/chart1.xml" */
    char *type;                  /* its content type */
    unsigned char *data;
    size_t length;
} owf_part;

typedef struct owf_book {
    int nsheets, capsheets;
    owf_sheet *sheets;
    int nstyles, capstyles;
    owf_cellstyle *styles;       /* styles[0] is the default */
    int nnumfmts, capnumfmts;
    char **numfmts;              /* numfmts[0] is "General" */
    int nfonts, capfonts;
    owf_font *fonts;
    int nnames, capnames;
    owf_defname *names;
    int date1904;
    int active;                  /* the sheet shown first */
    int nparts, capparts;        /* owf.library 2.1: parts kept as they were (a sheet's drawing...) */
    owf_part *parts;
} owf_book;

owf_book *owf_book_new(void);
void owf_book_free(owf_book *book);
/* Building a book (importers). NULL or -1 when memory runs out; the book
 * stays valid. */
owf_sheet *owf_book_add_sheet(owf_book *book, const char *name);
void owf_cellstyle_init(owf_cellstyle *style);
/* The index of an equal style, number format or font, added if new. */
int owf_book_style(owf_book *book, const owf_cellstyle *style);
int owf_book_numfmt(owf_book *book, const char *format);
int owf_book_font(owf_book *book, const char *name, owf_font_kind kind);
/* The cell at row, col: NULL when there is none (or, with create, when
 * memory ran out). A pointer stays good until the next cell is created. */
owf_cell *owf_sheet_cell(owf_sheet *sheet, unsigned row, unsigned col, int create);
const owf_cell *owf_sheet_find(const owf_sheet *sheet, unsigned row, unsigned col);
/* A cell's value; a text or formula is copied. */
void owf_cell_set_number(owf_cell *cell, double number);
int owf_cell_set_text(owf_cell *cell, const char *utf8, size_t length);
void owf_cell_set_bool(owf_cell *cell, int value);
void owf_cell_set_error(owf_cell *cell, int error);
int owf_cell_set_formula(owf_cell *cell, const char *text);
int owf_sheet_set_columns(owf_sheet *sheet, unsigned first, unsigned last, int width, int style, int hidden);
int owf_sheet_set_row(owf_sheet *sheet, unsigned row, int height, int hidden);
int owf_sheet_merge(owf_sheet *sheet, const owf_range *range);
int owf_book_add_name(owf_book *book, const char *name, int sheet, const char *formula, int hidden);
/* A new conditional format at the end of the sheet's, all zero (its
 * pointers NULL): the importer fills it in, its strings and ranges from
 * malloc. NULL when memory runs out. */
owf_condfmt *owf_sheet_add_condfmt(owf_sheet *sheet);
void owf_condfmt_free(owf_condfmt *cf);
/* A part kept (name, type and bytes copied), or found by its name (NULL:
 * none). -1 when memory runs out. A book's kept parts copied into another
 * that has none (-1: memory; it has none). */
int owf_book_keep_part(owf_book *book, const char *name, const char *type, const void *data, size_t length);
const owf_part *owf_book_part(const owf_book *book, const char *name);
int owf_book_copy_parts(owf_book *dst, const owf_book *src);
/* A new validation at the end of the sheet's, all zero (as
 * owf_sheet_add_condfmt), and one freed; the first whose ranges hold a
 * cell, or NULL. */
owf_validation *owf_sheet_add_validation(owf_sheet *sheet);
void owf_validation_free(owf_validation *v);
const owf_validation *owf_sheet_validation(const owf_sheet *sheet, unsigned row, unsigned col);
/* The comment on a cell, set (text and author copied; NULL text takes it
 * away), or found (NULL: none). -1 when memory runs out. */
int owf_sheet_set_comment(owf_sheet *sheet, unsigned row, unsigned col, const char *author, const char *text);
const owf_comment *owf_sheet_comment(const owf_sheet *sheet, unsigned row, unsigned col);
/* What a sheet holds beside its cells, columns, rows and merges (its
 * conditional formats and comments) copied from src into dst, which has
 * none: for a program that copies a sheet. -1 when memory runs out (dst
 * has none). Its validations, AutoFilter and drawing's name too. */
int owf_sheet_copy_extras(owf_sheet *dst, const owf_sheet *src);
void owf_sheet_free_extras(owf_sheet *sheet);

/* ---- Charts on sheets (owf.library 2.1) ----
 * The charts a sheet's kept drawing (an .xlsx's: owf_sheet.drawing) holds,
 * read from the book's kept parts, for a program to show them: where each
 * sits, its kind, title, legend and value axes, and its series (the first
 * group of a chart that has several): the cells their values and
 * categories (a scatter's x) come from, and the values and categories the
 * file last had; a pie's angle and colours by point, a doughnut's hole,
 * lines' and scatters' markers. A doughnut is a pie with a hole. */
enum { OWF_CHART_OTHER, OWF_CHART_COLUMN, OWF_CHART_BAR, OWF_CHART_LINE, OWF_CHART_AREA, OWF_CHART_PIE, OWF_CHART_SCATTER };
enum { OWF_CHART_CLUSTERED, OWF_CHART_STACKED, OWF_CHART_PERCENT };
/* what a series' data labels show, and where */
enum { OWF_LABEL_VALUE = 1, OWF_LABEL_PERCENT = 2, OWF_LABEL_CATEGORY = 4, OWF_LABEL_SERIES = 8 };
enum { OWF_LABEL_AUTO, OWF_LABEL_CENTRE, OWF_LABEL_INSIDE_END, OWF_LABEL_INSIDE_BASE, OWF_LABEL_OUTSIDE_END,
       OWF_LABEL_ABOVE, OWF_LABEL_BELOW, OWF_LABEL_LEFT, OWF_LABEL_RIGHT, OWF_LABEL_BEST_FIT };
/* a point's marker: AUTO, the chart's own */
enum { OWF_MARKER_AUTO, OWF_MARKER_NONE, OWF_MARKER_CIRCLE, OWF_MARKER_SQUARE, OWF_MARKER_DIAMOND, OWF_MARKER_TRIANGLE,
       OWF_MARKER_X, OWF_MARKER_STAR, OWF_MARKER_PLUS, OWF_MARKER_DASH, OWF_MARKER_DOT };
typedef struct {
    double value;
    int has_value;               /* 0: none (a gap) */
    char *label;                 /* its category, or NULL */
    double x;                    /* a scatter's: its x, when has_x */
    int has_x;
    unsigned long colour;        /* its own fill (a pie's slice): OWF_SHADE(rgb), or 0 */
} owf_chart_point;
typedef struct {
    char *name;                  /* or NULL */
    char *name_cell;             /* the cell its name comes from, "Chart!$B$1", or NULL */
    char *values;                /* the cells its values come from, "Chart!$B$1:$B$4", or NULL (only its points) */
    char *categories;            /* and its categories', or NULL */
    int npoints;
    owf_chart_point *points;     /* as the file last had them */
    unsigned long colour;        /* its fill (a line's colour): OWF_SHADE(rgb), or 0: the chart's own, by index */
    int index;
    char *x_values;              /* a scatter's: the cells its x values come from, or NULL */
    int line;                    /* lines and scatters: 1 its line is drawn, 0 not */
    int marker, marker_size;     /* OWF_MARKER_..., and its size in points (0: the chart's own) */
    unsigned labels;             /* its data labels: OWF_LABEL_VALUE...; 0 none */
    int label_pos;               /* OWF_LABEL_AUTO... */
    char *label_format;          /* their number format, or NULL: the values' own */
    char *format;                /* the values' number format as the file last had them, or NULL */
} owf_chart_series;
typedef struct {
    int kind, grouping;          /* OWF_CHART_... */
    char *title;                 /* or NULL */
    int legend;                  /* where: 'r', 'l', 't', 'b'; 0 none */
    int gap;                     /* bars: the gap between categories, a percentage of a bar's width */
    int gridlines;               /* the value axis's major gridlines */
    int has_min, has_max;
    double min, max;             /* the value axis's, when set */
    /* where: from a cell's top left corner and an offset (EMU, 914400 an
     * inch, 9525 a pixel at 96 dpi) to another cell's, or a size from there */
    unsigned from_col, from_row, to_col, to_row;
    long from_col_off, from_row_off, to_col_off, to_row_off;
    int sized;                   /* 1: width and height, not to_* */
    long width, height;
    int nseries;
    owf_chart_series *series;
    int hole;                    /* a doughnut's hole, a percentage of its size; 0 a pie */
    int first_angle;             /* a pie's first slice, degrees clockwise from 12 o'clock */
    int vary;                    /* each point (a pie's slice) in its own colour */
    int x_gridlines;             /* a scatter's x axis (the value axis above is its y): its gridlines and limits */
    int has_xmin, has_xmax;
    double xmin, xmax;
    char *value_format;          /* the value axis's number format, or NULL: the values' own */
    char *x_format;              /* a scatter's x axis's */
} owf_chart;
/* The sheet's charts (none: *count 0), freed with owf_charts_free. OWF_OK,
 * or OWF_ERR_MEMORY. */
int owf_sheet_charts(const owf_book *book, int sheet, owf_chart **charts, int *count);
void owf_charts_free(owf_chart *charts, int count);

/* A number as a formula or a file writes it: up to 15 significant digits,
 * "0.1", "12", "1.5E+20". out holds at least 32 bytes. */
void owf_number_text(double number, char *out);

/* Workbook formats, as the document ones. */
typedef struct owf_book_format {
    const char *name;            /* "xls", "csv"... */
    const char *description;
    const char *extensions;
    int (*detect)(const unsigned char *data, size_t length);
    int (*import)(const unsigned char *data, size_t length, owf_book *book, owf_report *report);
    int (*export)(const owf_book *book, unsigned char **data, size_t *length, owf_report *report);
} owf_book_format;

int owf_book_format_count(void);
const owf_book_format *owf_book_format_at(int index);
const owf_book_format *owf_book_format_named(const char *name);
const owf_book_format *owf_book_format_for_filename(const char *filename);
int owf_import_book_memory(const unsigned char *data, size_t length, const char *format,
                           owf_book **book, owf_report *report, const owf_book_format **used);
int owf_import_book_file(const char *path, const char *format, owf_book **book,
                         owf_report *report, const owf_book_format **used);
int owf_export_book_file(const owf_book *book, const char *path, const char *format, owf_report *report);

/* ---- What a file is (owf.library 2) ----
 * Which of the suite's programs a file is for, from its bytes: ODF
 * packages by their mimetype, OOXML packages by their main part, compound
 * files by their streams (Publisher's by owf_pub_detect), OpenBase's
 * databases by SQLite's header and their application id ("OADB"); any
 * other file by the best score of the document and workbook format
 * tables. Plain text is a document, CSV included: the bytes can't tell
 * them apart (a program with the file's name can ask
 * owf_book_format_for_filename). */
typedef enum {
    OWF_KIND_UNKNOWN, OWF_KIND_DOCUMENT, OWF_KIND_WORKBOOK, OWF_KIND_DECK, OWF_KIND_DATABASE,
    OWF_KIND_PUBLICATION         /* Microsoft Publisher */
} owf_kind;
owf_kind owf_file_kind(const unsigned char *data, size_t length);

/* ---- The structures' sizes (owf.library 2) ----
 * Programs index owf.library's arrays themselves (a story's paragraphs, a
 * sheet's cells), so each structure's size is part of the library's ABI.
 * owf_struct_size says what the library was built with (0 for a number it
 * doesn't know), and owf_open() (owf_open.h) refuses a library whose sizes
 * differ from the program's. New structures go at the end. */
enum {
    OWF_STRUCT_CHARFMT, OWF_STRUCT_RUN, OWF_STRUCT_TAB, OWF_STRUCT_PARAFMT, OWF_STRUCT_PARA, OWF_STRUCT_STORY,
    OWF_STRUCT_FONT, OWF_STRUCT_PAGE, OWF_STRUCT_IMAGE, OWF_STRUCT_DOC, OWF_STRUCT_FORMAT, OWF_STRUCT_CELL,
    OWF_STRUCT_CELLSTYLE, OWF_STRUCT_COLINFO, OWF_STRUCT_ROWINFO, OWF_STRUCT_RANGE, OWF_STRUCT_SHEET,
    OWF_STRUCT_DEFNAME, OWF_STRUCT_BOOK, OWF_STRUCT_BOOK_FORMAT, OWF_STRUCT_UNZIP, OWF_STRUCT_PUB,
    OWF_STRUCT_PUB_PAGE, OWF_STRUCT_PUB_FRAME,
    OWF_STRUCT_CONDFMT, OWF_STRUCT_COMMENT, OWF_STRUCT_VALIDATION, OWF_STRUCT_PART, OWF_STRUCT_FRAME,
    OWF_STRUCT_CHART_POINT, OWF_STRUCT_CHART_SERIES, OWF_STRUCT_CHART,
    OWF_STRUCT_COUNT
};
unsigned long owf_struct_size(int which);
/* sizeof each, in that order, where owf_pub.h and owf_zip.h are included */
#define OWF_STRUCT_SIZES                                                                                   \
    sizeof(owf_charfmt), sizeof(owf_run), sizeof(owf_tab), sizeof(owf_parafmt), sizeof(owf_para),        \
    sizeof(owf_story), sizeof(owf_font), sizeof(owf_page), sizeof(owf_image), sizeof(owf_doc),            \
    sizeof(owf_format), sizeof(owf_cell), sizeof(owf_cellstyle), sizeof(owf_colinfo), sizeof(owf_rowinfo), \
    sizeof(owf_range), sizeof(owf_sheet), sizeof(owf_defname), sizeof(owf_book), sizeof(owf_book_format), \
    sizeof(owf_unzip), sizeof(owf_pub), sizeof(owf_pub_page), sizeof(owf_pub_frame), sizeof(owf_condfmt), \
    sizeof(owf_comment), sizeof(owf_validation), sizeof(owf_part), sizeof(owf_frame), sizeof(owf_chart_point), \
    sizeof(owf_chart_series), sizeof(owf_chart)

#ifdef __cplusplus
}
#endif

/* The calls through owf.library (its inline calls in libowf/library/sdk),
 * once every header that declares calls is complete, whichever of them a
 * program includes first: owf_calc.h, owf_pub.h and owf_zip.h end the same
 * way. (The inline calls are macros of the calls' names, so they must come
 * after every declaration.) */
#define OWF_H_DONE
#if defined(OWF_USE_LIBRARY) && !defined(OWF_LIBRARY_BUILD)
#include "owf_calc.h"
#include "owf_pub.h"
#include "owf_zip.h"
#if defined(OWF_CALC_H_DONE) && defined(OWF_PUB_H_DONE) && defined(OWF_ZIP_H_DONE)
#include <proto/owf.h>
#endif
#endif

#endif
