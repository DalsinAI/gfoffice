/*
 * libowf: OpenDocument spreadsheets (.ods, .ots, and flat .fods), read from
 * OASIS ODF 1.2 (and 1.3's additions LibreOffice writes). Sheets and their
 * cells: numbers, percentages, currencies, dates and times (as day
 * numbers), booleans, text (paragraphs, spaces, tabs and line breaks),
 * errors as LibreOffice marks them; formulas in OpenFormula, turned into
 * Excel's text, with their last results; cell styles through their parents
 * (fonts, sizes, bold, italic, underline, strike-through, raised and
 * lowered text, colours, number formats, alignment, wrap, indents,
 * backgrounds, borders); column widths and row heights, hidden rows,
 * columns and sheets, merged cells, array formulas, frozen panes and the
 * active sheet (settings.xml), named ranges and expressions, the 1904
 * date system (the null date), comments on cells (office:annotation), data
 * validation (table:content-validations and the cells that name them),
 * AutoFilters (a database range with its buttons, and its conditions of
 * values as Excel's) and conditional formatting (LibreOffice's
 * calcext:conditional-formats: conditions with their styles' looks,
 * colour scales, data bars and icon sets).
 *
 * The XML is read as a stream, element by element: a sheet's memory is its
 * cells'. Repeated rows and cells (number-rows-repeated, the empty rows to
 * the end of a sheet) cost nothing unless they hold something, and what
 * they may make is bounded.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "owf_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NS_OFFICE "urn:oasis:names:tc:opendocument:xmlns:office:1.0"
#define NS_STYLE "urn:oasis:names:tc:opendocument:xmlns:style:1.0"
#define NS_TEXT "urn:oasis:names:tc:opendocument:xmlns:text:1.0"
#define NS_TABLE "urn:oasis:names:tc:opendocument:xmlns:table:1.0"
#define NS_FO "urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0"
#define NS_SVG "urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0"
#define NS_NUMBER "urn:oasis:names:tc:opendocument:xmlns:datastyle:1.0"
#define NS_CONFIG "urn:oasis:names:tc:opendocument:xmlns:config:1.0"
#define NS_DRAW "urn:oasis:names:tc:opendocument:xmlns:drawing:1.0"
#define NS_CALCEXT "urn:org:documentfoundation:names:experimental:calc:xmlns:calcext:1.0"
#define NS_LOEXT "urn:org:documentfoundation:names:experimental:office:xmlns:loext:1.0"
#define NS_DC "http://purl.org/dc/elements/1.1/"
#define NS_CHART "urn:oasis:names:tc:opendocument:xmlns:chart:1.0"
#define NS_XLINK "http://www.w3.org/1999/xlink"
/* libowf's own attributes: Excel's text of a number format, a sheet's default
 * column width and row height, which ODF has no place for. Other programs
 * ignore them, as ODF lets them; libowf reads them back. */
#define NS_GFO "urn:org:amigachrome:names:experimental:gfoffice:xmlns:calc:1.0"

#define MAX_ROWS 1048576UL
#define MAX_COLS 16384UL
/* What repeats may make, for the whole book: a few bytes of a file must
 * not become gigabytes of cells. */
#define REPEAT_BUDGET 1048576UL          /* cells */
#define EMPTY_BUDGET 262144UL            /* empty cells with a style */
#define ROW_BUDGET 262144UL              /* row heights */
#define ARRAY_BUDGET 262144UL            /* cells of array formulas' ranges */
#define TEXT_BUDGET (64UL * 1024 * 1024) /* bytes of text copied into repeated cells */
#define MAX_ARRAYS 4096
#define MAX_CELL_TEXT 131072UL           /* bytes of a cell's text: Excel's 32767 characters and more */

/* ---- Styles ---- */

enum { F_CELL = 1, F_COLUMN, F_ROW, F_TABLE, F_OTHER };
enum {
    P_FONT = 1 << 0, P_SIZE = 1 << 1, P_COLOUR = 1 << 2, P_HALIGN = 1 << 3, P_VALIGN = 1 << 4, P_WRAP = 1 << 5,
    P_INDENT = 1 << 6, P_FILL = 1 << 7, P_DATA = 1 << 8, P_BORDER = 1 << 9 /* and three more, a side each */
};

typedef struct {
    char *name, *parent, *data_style, *font;
    int family, font_kind;
    unsigned mask;
    unsigned flag_mask, flags;           /* OWF_BOLD... set, and their values */
    int size;
    unsigned long colour;                /* or OWF_COLOUR_AUTO */
    int halign, valign, wrap, indent;
    unsigned long fill;
    unsigned char border[4];
    unsigned long border_colour[4];
    int width;                           /* columns' */
    int height, optimal;                 /* rows' */
    int display;                         /* tables': 0 hidden */
    int model;                           /* the book's style, -1 not yet made */
    int is_default;                      /* style:default-style */
} ods_style;

enum { N_NUMBER, N_CURRENCY, N_PERCENT, N_DATE, N_TIME, N_BOOLEAN, N_TEXT };

typedef struct {
    char *cond;
    char *apply;
} ods_map;

typedef struct {
    char *name;
    int kind;
    owf_buf code;                        /* this section's Excel text, as the elements come */
    char *excel;                         /* libowf's own record of the Excel text, or NULL */
    int elapsed;                         /* time-style truncate-on-overflow="false" */
    int general;                         /* a lone number:number without decimals */
    int elements;
    char colour[16];
    ods_map maps[4];
    int nmaps;
} ods_numstyle;

typedef struct {
    char *name, *family;
    int kind;
} ods_face;

/* ---- The reader ---- */

typedef struct {
    unsigned long first, last;
    const char *cell_style;              /* table:default-cell-style-name */
} ods_colrun;

typedef struct {
    owf_range range;
    char *text;
} ods_array;

typedef struct {
    char *name, *formula;
    int sheet;
} ods_name;

typedef struct {
    char *sheet;
    int hmode, vmode;
    long hpos, vpos;
} ods_split;

typedef struct {
    char *name;
    owf_validation v;                    /* without its ranges: the cells naming it give them */
    int ok;                              /* its condition read */
} ods_rule;

typedef struct {
    int rule;
    owf_range *ranges;
    int nranges, capranges;
} ods_use;                               /* a rule's cells on the sheet being read */

typedef struct {
    int rule;
    unsigned long first, last;
} ods_run;                               /* a rule's cells in the row being read */

typedef struct {
    int sheet;
    unsigned row, col;                   /* the cell it starts in; absolute: x, y from the sheet's corner */
    int absolute;
    long x, y, width, height;            /* EMU */
    int has_end;
    unsigned end_row, end_col;
    long end_x, end_y;
    char *object;                        /* "Object 1": its chart document in the package */
    char *xml;                           /* or (a flat file) the chart document itself, as XML */
    long z;                              /* draw:z-index: the order the sheet's charts go in */
} ods_frame;                             /* a chart's frame */

typedef struct {
    owf_book *book;
    owf_report *report;
    int failed, damaged;
    const char *ext_ns;                  /* libowf's own attributes' namespace */

    ods_style *styles;
    int nstyles, capstyles;
    ods_style *style;                    /* being read */
    ods_numstyle *nums;
    int nnums, capnums;
    ods_numstyle *num;                   /* being read */
    int num_text, num_text_depth;        /* inside number:text or currency-symbol */
    int num_element;                     /* which: 1 text, 2 currency symbol */
    size_t num_text_start;
    ods_face *faces;
    int nfaces, capfaces;
    const char *last_name;               /* the cell style looked up last */
    int last_model;

    /* the body */
    int in_body, null_date_1904, null_date_other;
    owf_sheet *sheet;
    int table_depth;                     /* the table:table being read, 0 none */
    unsigned long row, col;
    unsigned long row_repeat;
    const char *row_style, *row_cell_style;
    int row_hidden;
    int row_first_cell;                  /* the row's first cell in sheet->cells when it began */
    ods_colrun *cols;
    int ncols, capcols;
    unsigned long ncolumns;              /* columns described so far */
    unsigned long repeat_budget, empty_budget, row_budget, array_budget, text_budget;
    ods_array *arrays;
    int narrays, caparrays;
    int default_width, default_height;   /* from libowf's own attributes, or -1 */

    /* the cell being read */
    int in_cell, skip_depth;
    const owf_xml_attribute *cell_attrs;
    int cell_nattrs;
    owf_xml_attribute cattrs[32];
    int ncattrs;
    owf_buf text;
    int paras, in_p;

    /* named ranges and expressions, made once the sheets are known */
    ods_name *names;
    int nnames, capnames;

    /* settings */
    char *active;
    ods_split *splits;
    int nsplits, capsplits;
    int cfg_views, cfg_view, cfg_tables, cfg_item, cfg_done;
    const char *cfg_entry, *cfg_item_name;
    owf_buf cfg_text;

    int notes_cell;

    /* data validation: the rules, the one being read (its number, 0 none), its message and text */
    ods_rule *rules;
    int nrules, caprules, rule_cur, rule_msg, rule_p, rule_paras;
    owf_buf rule_text;
    ods_use *uses;
    int nuses, capuses;
    ods_run *runs;
    int nruns, capruns;
    /* the comment of the cell being read */
    int note_depth, note_p, note_who, note_paras, note_ready;
    owf_buf note_text, note_author;
    /* the AutoFilter being read: its sheet, Excel's filterColumn elements */
    owf_sheet *af_sheet;
    owf_buf af_xml;
    int af_field, af_values, af_or, af_approx;
    char *af_value;                      /* the filter-condition's own value, for one without a set */
    /* conditional formatting: the ranges of the conditional-format being read, the rules' priorities so far,
     * the data bar or icon set being read (its rule, -1 none) and its cfvo elements */
    owf_range *cf_ranges;
    int cf_nranges, cf_capranges, cf_priority, cf_kept, cf_seen, style_maps;
    owf_buf cf_xml;
    /* charts: their frames; the frame being read (its depth, 0 none), an inline chart document being copied */
    ods_frame *frames;
    int nframes, capframes, frame_depth, frame_ok, inline_depth, in_shapes;
    owf_buf inline_xml;
} ods;

static void rule_cell(ods *o, const char *name, unsigned long first, unsigned long last);
static void inline_start(ods *o, const owf_xml_event *e);
static void rule_row(ods *o, unsigned long n);
static void rule_table(ods *o);

static void *grow(ods *o, void *items, int count, int *capacity, size_t size)
{
    void *p;
    int want;
    if (count < *capacity)
        return items;
    if (*capacity > 0x3FFFFFFF / 2) {
        o->failed = 1;
        return NULL;
    }
    want = *capacity ? *capacity * 2 : 16;
    p = realloc(items, (size_t)want * size);
    if (!p) {
        o->failed = 1;
        return NULL;
    }
    *capacity = want;
    return p;
}

static char *copy(ods *o, const char *s)
{
    size_t n;
    char *out;
    if (!s)
        return NULL;
    n = strlen(s);
    out = malloc(n + 1);
    if (!out) {
        o->failed = 1;
        return NULL;
    }
    memcpy(out, s, n + 1);
    return out;
}

static const char *ev(const owf_xml_event *e, const char *ns, const char *name)
{
    return owf_xml_event_attr(e, ns, name);
}

static int is(const owf_xml_event *e, const char *ns, const char *name)
{
    return !strcmp(e->ns, ns) && !strcmp(e->name, name);
}

static long whole(const char *v, long def, long min, long max)
{
    char *end;
    long n;
    if (!v)
        return def;
    n = strtol(v, &end, 10);
    if (end == v || *end || n < min || n > max)
        return def;
    return n;
}

/* A length ("2.258cm", "0.74pt") as twips; 0 when it is not one. Lengths
 * with more than six digits before the point are not lengths a sheet has. */
static int length(const char *s, int *twips)
{
    const char *p = s;
    int digits = 0;
    if (!s)
        return 0;
    if (*p == '-' || *p == '+')
        p++;
    while (*p >= '0' && *p <= '9') {
        p++;
        digits++;
    }
    return digits <= 6 && owf_parse_length(s, twips);
}

/* ---- Fonts ---- */

static void read_face(ods *o, const owf_xml_event *e)
{
    const char *name = ev(e, NS_STYLE, "name"), *family = ev(e, NS_SVG, "font-family"),
               *generic = ev(e, NS_STYLE, "font-family-generic");
    ods_face *f;
    if (!name)
        return;
    if (!(f = grow(o, o->faces, o->nfaces, &o->capfaces, sizeof *f)))
        return;
    o->faces = f;
    f = &o->faces[o->nfaces];
    f->name = copy(o, name);
    f->family = copy(o, family ? family : name);
    f->kind = !generic ? OWF_FONT_ANY : !strcmp(generic, "roman") ? OWF_FONT_SERIF
              : !strcmp(generic, "swiss") ? OWF_FONT_SANS : !strcmp(generic, "modern") ? OWF_FONT_MONO : OWF_FONT_ANY;
    if (f->family) {                     /* svg:font-family may be quoted: 'Liberation Serif' */
        size_t n = strlen(f->family);
        if (n >= 2 && (f->family[0] == '\'' || f->family[0] == '"') && f->family[n - 1] == f->family[0]) {
            memmove(f->family, f->family + 1, n - 2);
            f->family[n - 2] = 0;
        }
    }
    if (f->name && f->family)
        o->nfaces++;
    else {
        free(f->name);
        free(f->family);
    }
}

static const ods_face *find_face(const ods *o, const char *name)
{
    int i;
    for (i = 0; name && i < o->nfaces; i++)
        if (!strcmp(o->faces[i].name, name))
            return &o->faces[i];
    return NULL;
}

/* ---- Cell, column, row and table styles ---- */

static void start_style(ods *o, const owf_xml_event *e, int is_default)
{
    const char *family = ev(e, NS_STYLE, "family"), *name = ev(e, NS_STYLE, "name");
    ods_style *s;
    o->style = NULL;
    if (!family || (!name && !is_default))
        return;
    if (!(s = grow(o, o->styles, o->nstyles, &o->capstyles, sizeof *s)))
        return;
    o->styles = s;
    s = &o->styles[o->nstyles];
    memset(s, 0, sizeof *s);
    s->family = !strcmp(family, "table-cell") ? F_CELL : !strcmp(family, "table-column") ? F_COLUMN
                : !strcmp(family, "table-row") ? F_ROW : !strcmp(family, "table") ? F_TABLE : F_OTHER;
    if (s->family == F_OTHER)
        return;
    s->name = copy(o, is_default ? "" : name);
    s->parent = copy(o, ev(e, NS_STYLE, "parent-style-name"));
    s->data_style = copy(o, ev(e, NS_STYLE, "data-style-name"));
    if (s->data_style)
        s->mask |= P_DATA;
    s->model = -1;
    s->display = 1;
    s->optimal = 1;
    s->is_default = is_default;
    if (!s->name)
        return;
    o->nstyles++;
    o->style = s;
}

/* "0.74pt solid #000000" */
static void read_border(const char *v, unsigned char *line, unsigned long *colour)
{
    char word[32];
    int width = 15, n;
    const char *p = v;
    *line = OWF_LINE_NONE;
    *colour = 0;
    if (!v)
        return;
    while (*p) {
        while (*p == ' ')
            p++;
        for (n = 0; p[n] && p[n] != ' ' && n < 31; n++)
            word[n] = p[n];
        word[n] = 0;
        while (*p && *p != ' ')
            p++;
        if (!n)
            break;
        if (word[0] == '#')
            owf_parse_colour(word, colour) ? (void)(*colour = OWF_SHADE(*colour)) : (void)0;
        else if ((word[0] >= '0' && word[0] <= '9') || word[0] == '.')
            length(word, &width);
        else if (!strcmp(word, "none") || !strcmp(word, "hidden"))
            *line = 255;
        else if (!strcmp(word, "solid"))
            *line = width < 8 ? OWF_LINE_HAIR : width <= 20 ? OWF_LINE_THIN : width < 40 ? OWF_LINE_MEDIUM : OWF_LINE_THICK;
        else if (!strcmp(word, "double") || !strcmp(word, "double-thin"))
            *line = OWF_LINE_DOUBLE;
        else if (!strcmp(word, "dotted"))
            *line = OWF_LINE_DOTTED;
        else if (!strcmp(word, "dashed"))
            *line = width <= 20 ? OWF_LINE_DASHED : OWF_LINE_MEDIUM_DASHED;
        else if (!strcmp(word, "fine-dashed"))
            *line = width <= 20 ? OWF_LINE_DASHED : OWF_LINE_SLANTED_DASH_DOT;
        else if (!strcmp(word, "dash-dot"))
            *line = width <= 20 ? OWF_LINE_DASH_DOT : OWF_LINE_MEDIUM_DASH_DOT;
        else if (!strcmp(word, "dash-dot-dot"))
            *line = width <= 20 ? OWF_LINE_DASH_DOT_DOT : OWF_LINE_MEDIUM_DASH_DOT_DOT;
        else if (!strcmp(word, "groove") || !strcmp(word, "ridge") || !strcmp(word, "inset") || !strcmp(word, "outset"))
            *line = OWF_LINE_THIN;
    }
    if (*line == 255 || *line == OWF_LINE_NONE) {
        *line = OWF_LINE_NONE;
        *colour = 0;
    }
}

static void set_flag(ods_style *s, unsigned flag, int on)
{
    s->flag_mask |= flag;
    if (on)
        s->flags |= flag;
    else
        s->flags &= ~flag;
}

static void style_properties(ods *o, const owf_xml_event *e)
{
    ods_style *s = o->style;
    const char *v;
    int twips;
    unsigned long rgb;
    static const char *const sides[4] = { "border-left", "border-right", "border-top", "border-bottom" };
    int i;

    if (!s || strcmp(e->ns, NS_STYLE))
        return;
    if (!strcmp(e->name, "table-cell-properties")) {
        if ((v = ev(e, NS_FO, "background-color")) != NULL) {
            s->mask |= P_FILL;
            s->fill = owf_parse_colour(v, &rgb) ? OWF_SHADE(rgb) : 0;
        }
        if ((v = ev(e, NS_FO, "border")) != NULL) {
            unsigned char line;
            unsigned long colour;
            read_border(v, &line, &colour);
            for (i = 0; i < 4; i++) {
                s->border[i] = line;
                s->border_colour[i] = colour;
                s->mask |= P_BORDER << i;
            }
        }
        for (i = 0; i < 4; i++)
            if ((v = ev(e, NS_FO, sides[i])) != NULL) {
                read_border(v, &s->border[i], &s->border_colour[i]);
                s->mask |= P_BORDER << i;
            }
        if ((v = ev(e, NS_STYLE, "vertical-align")) != NULL) {
            s->mask |= P_VALIGN;
            s->valign = !strcmp(v, "top") ? OWF_VALIGN_TOP : !strcmp(v, "middle") ? OWF_VALIGN_CENTRE : OWF_VALIGN_BOTTOM;
        }
        if ((v = ev(e, NS_FO, "wrap-option")) != NULL) {
            s->mask |= P_WRAP;
            s->wrap = !strcmp(v, "wrap");
        }
        if ((v = ev(e, NS_STYLE, "rotation-angle")) != NULL && strcmp(v, "0") && strcmp(v, "0deg"))
            owf_report_add(o->report, OWF_NOTE_APPROX, "Text turned at an angle in cells is shown level");
        if ((v = ev(e, NS_STYLE, "text-align-source")) != NULL && !strcmp(v, "value-type")) {
            s->mask |= P_HALIGN;
            s->halign = OWF_HALIGN_GENERAL;
        }
        if ((v = ev(e, NS_STYLE, "repeat-content")) != NULL && !strcmp(v, "true")) {
            s->mask |= P_HALIGN;
            s->halign = OWF_HALIGN_FILL;
        }
        if (ev(e, NS_STYLE, "diagonal-bl-tr") || ev(e, NS_STYLE, "diagonal-tl-br")) {
            const char *a = ev(e, NS_STYLE, "diagonal-bl-tr"), *b = ev(e, NS_STYLE, "diagonal-tl-br");
            if ((a && strcmp(a, "none")) || (b && strcmp(b, "none")))
                owf_report_add(o->report, OWF_NOTE_LOST, "Diagonal lines across cells are not kept");
        }
    } else if (!strcmp(e->name, "paragraph-properties")) {
        if ((v = ev(e, NS_FO, "text-align")) != NULL && !(s->mask & P_HALIGN && s->halign == OWF_HALIGN_FILL)) {
            s->mask |= P_HALIGN;
            s->halign = !strcmp(v, "center") ? OWF_HALIGN_CENTRE : !strcmp(v, "end") || !strcmp(v, "right") ? OWF_HALIGN_RIGHT
                        : !strcmp(v, "justify") ? OWF_HALIGN_JUSTIFY : OWF_HALIGN_LEFT;
        }
        if (length(ev(e, NS_FO, "margin-left"), &twips)) {
            s->mask |= P_INDENT;
            s->indent = twips > 0 ? (twips + 75) / 150 : 0;   /* a level is 150 twips, as LibreOffice has it */
            if (s->indent > 255)
                s->indent = 255;
        }
    } else if (!strcmp(e->name, "text-properties")) {
        if ((v = ev(e, NS_STYLE, "font-name")) != NULL) {
            const ods_face *f = find_face(o, v);
            free(s->font);
            s->font = copy(o, f ? f->family : v);
            s->font_kind = f ? f->kind : -1;
            s->mask |= P_FONT;
        } else if ((v = ev(e, NS_FO, "font-family")) != NULL) {
            free(s->font);
            s->font = copy(o, v);
            s->font_kind = -1;
            s->mask |= P_FONT;
        }
        if (length(ev(e, NS_FO, "font-size"), &twips) && twips > 0 && twips < 32767) {
            s->mask |= P_SIZE;
            s->size = twips;
        }
        if ((v = ev(e, NS_FO, "font-weight")) != NULL)
            set_flag(s, OWF_BOLD, !strcmp(v, "bold") || (v[0] >= '6' && v[0] <= '9' && v[1] == '0' && v[2] == '0'));
        if ((v = ev(e, NS_FO, "font-style")) != NULL)
            set_flag(s, OWF_ITALIC, !strcmp(v, "italic") || !strcmp(v, "oblique"));
        if ((v = ev(e, NS_STYLE, "text-underline-style")) != NULL)
            set_flag(s, OWF_UNDERLINE, strcmp(v, "none") != 0);
        if ((v = ev(e, NS_STYLE, "text-line-through-style")) != NULL)
            set_flag(s, OWF_STRIKE, strcmp(v, "none") != 0);
        if ((v = ev(e, NS_STYLE, "text-position")) != NULL) {
            int super = !strncmp(v, "super", 5) || (v[0] >= '1' && v[0] <= '9'),
                sub = !strncmp(v, "sub", 3) || v[0] == '-';
            set_flag(s, OWF_SUPER, super);
            set_flag(s, OWF_SUB, sub && !super);
        }
        if ((v = ev(e, NS_STYLE, "use-window-font-color")) != NULL && !strcmp(v, "true")) {
            s->mask |= P_COLOUR;
            s->colour = OWF_COLOUR_AUTO;
        } else if (owf_parse_colour(ev(e, NS_FO, "color"), &rgb)) {
            s->mask |= P_COLOUR;
            s->colour = rgb;
        }
    } else if (!strcmp(e->name, "table-column-properties")) {
        if (length(ev(e, NS_STYLE, "column-width"), &twips) && twips >= 0)
            s->width = twips;
    } else if (!strcmp(e->name, "table-row-properties")) {
        if (length(ev(e, NS_STYLE, "row-height"), &twips) && twips >= 0)
            s->height = twips;
        if ((v = ev(e, NS_STYLE, "use-optimal-row-height")) != NULL)
            s->optimal = !strcmp(v, "true");
    } else if (!strcmp(e->name, "table-properties")) {
        if ((v = ev(e, NS_TABLE, "display")) != NULL)
            s->display = strcmp(v, "false") != 0;
    } else if (!strcmp(e->name, "map") && s->family == F_CELL) {
        o->style_maps = 1;                       /* said once the body shows whether LibreOffice's are there too */
    }
}

static const ods_style *find_style(const ods *o, const char *name, int family)
{
    int i;
    if (!name)
        return NULL;
    for (i = o->nstyles - 1; i >= 0; i--)   /* automatic styles come after, and win */
        if (o->styles[i].family == family && !o->styles[i].is_default && !strcmp(o->styles[i].name, name))
            return &o->styles[i];
    return NULL;
}

static const ods_style *default_style(const ods *o, int family)
{
    int i;
    for (i = 0; i < o->nstyles; i++)
        if (o->styles[i].family == family && o->styles[i].is_default)
            return &o->styles[i];
    return NULL;
}

/* ---- Number styles: ODF's elements as Excel's text ---- */

static void start_numstyle(ods *o, const owf_xml_event *e)
{
    static const char *const kinds[7] = { "number-style", "currency-style", "percentage-style", "date-style",
                                          "time-style", "boolean-style", "text-style" };
    ods_numstyle *n;
    const char *name = ev(e, NS_STYLE, "name"), *v;
    int k;
    o->num = NULL;
    for (k = 0; k < 7 && strcmp(e->name, kinds[k]); k++)
        ;
    if (k == 7 || !name)
        return;
    if (!(n = grow(o, o->nums, o->nnums, &o->capnums, sizeof *n)))
        return;
    o->nums = n;
    n = &o->nums[o->nnums];
    memset(n, 0, sizeof *n);
    owf_buf_init(&n->code);
    n->kind = k;
    n->name = copy(o, name);
    if (o->ext_ns && (v = ev(e, o->ext_ns, "format-code")) != NULL)
        n->excel = copy(o, v);
    v = ev(e, NS_NUMBER, "truncate-on-overflow");
    n->elapsed = v && !strcmp(v, "false");
    if (!n->name)
        return;
    o->nnums++;
    o->num = n;
}

static void put_zeros(owf_buf *b, int c, long n)
{
    for (; n > 0; n--)
        owf_buf_putc(b, c);
}

/* number:number's integer and decimal digits: "#,##0.00" */
static void put_digits(owf_buf *b, const owf_xml_event *e, int scientific)
{
    long decimals = whole(ev(e, NS_NUMBER, "decimal-places"), 0, 0, 30);
    long min_dec = whole(ev(e, NS_NUMBER, "min-decimal-places"), decimals, 0, 30);
    long integers = whole(ev(e, NS_NUMBER, "min-integer-digits"), 0, 0, 30);
    long interval = scientific ? whole(ev(e, NS_NUMBER, "exponent-interval"), 1, 1, 30) : 1;
    const char *grouping = ev(e, NS_NUMBER, "grouping");
    long i, total;
    if (min_dec > decimals)
        min_dec = decimals;
    if (grouping && !strcmp(grouping, "true") && !scientific) {
        total = integers > 4 ? integers : 4;
        for (i = total; i > 0; i--) {
            owf_buf_putc(b, i <= integers ? '0' : '#');
            if (i == 4 || (i > 4 && (i - 1) % 3 == 0))
                owf_buf_putc(b, ',');
        }
    } else {
        total = interval > integers ? interval : integers;
        if (!total)
            owf_buf_putc(b, '#');
        for (i = total; i > 0; i--)
            owf_buf_putc(b, i <= integers ? '0' : '#');
    }
    if (decimals) {
        owf_buf_putc(b, '.');
        put_zeros(b, '0', min_dec);
        put_zeros(b, '#', decimals - min_dec);
    }
}

/* Text in a format: as it is when Excel shows those characters plainly,
 * else in quotes. */
static void put_literal(owf_buf *b, const char *s, size_t n, int kind)
{
    static const char plain[] = " $-+/():!^&'~{}<>=";
    size_t i;
    int all_plain = 1;
    for (i = 0; i < n; i++)
        if (!memchr(plain, s[i], sizeof plain - 1) && !((kind == N_DATE || kind == N_TIME) && (s[i] == '.' || s[i] == ',')))
            all_plain = 0;
    if (!n)
        return;
    if (kind == N_PERCENT && n == 1 && s[0] == '%') {
        owf_buf_putc(b, '%');
        return;
    }
    if (all_plain) {
        owf_buf_put(b, s, n);
        return;
    }
    owf_buf_putc(b, '"');
    for (i = 0; i < n; i++) {
        if (s[i] == '"')
            owf_buf_puts(b, "\"\\\"\"");     /* a quote: out of the quotes, escaped */
        else
            owf_buf_putc(b, s[i]);
    }
    owf_buf_putc(b, '"');
}

static void number_element(ods *o, const owf_xml_event *e)
{
    ods_numstyle *n = o->num;
    owf_buf *b = &n->code;
    const char *name = e->name, *style = ev(e, NS_NUMBER, "style"), *v;
    int is_long = style && !strcmp(style, "long");
    n->elements++;
    if (!strcmp(name, "number")) {
        if (!ev(e, NS_NUMBER, "decimal-places") && whole(ev(e, NS_NUMBER, "min-integer-digits"), 1, 0, 30) <= 1 &&
            !ev(e, NS_NUMBER, "grouping") && !ev(e, NS_NUMBER, "display-factor"))
            n->general = 1;
        put_digits(b, e, 0);
        if ((v = ev(e, NS_NUMBER, "display-factor")) != NULL) {
            long f = whole(v, 1, 1, 0x7FFFFFFFL);
            for (; f >= 1000; f /= 1000)
                owf_buf_putc(b, ',');
        }
    } else if (!strcmp(name, "scientific-number")) {
        long digits = whole(ev(e, NS_NUMBER, "min-exponent-digits"), 2, 1, 10);
        v = ev(e, NS_NUMBER, "forced-exponent-sign");
        put_digits(b, e, 1);
        owf_buf_puts(b, v && !strcmp(v, "false") ? "E-" : "E+");
        put_zeros(b, '0', digits);
    } else if (!strcmp(name, "fraction")) {
        const char *ints = ev(e, NS_NUMBER, "min-integer-digits");
        long num = whole(ev(e, NS_NUMBER, "min-numerator-digits"), 1, 1, 10);
        long den = whole(ev(e, NS_NUMBER, "min-denominator-digits"), 1, 1, 10);
        long value = whole(ev(e, NS_NUMBER, "denominator-value"), 0, 0, 0x7FFFFFFFL);
        if (ints) {
            long i = whole(ints, 0, 0, 30);
            if (i)
                put_zeros(b, '0', i);
            else
                owf_buf_putc(b, '#');
            owf_buf_putc(b, ' ');
        }
        put_zeros(b, '?', num);
        owf_buf_putc(b, '/');
        if (value)
            owf_buf_printf(b, "%ld", value);
        else
            put_zeros(b, '?', den);
    } else if (!strcmp(name, "text") || !strcmp(name, "currency-symbol")) {
        o->num_text = 1;
        o->num_element = name[0] == 't' ? 1 : 2;
        o->num_text_depth = e->depth;
        if (o->num_element == 2)
            owf_buf_puts(b, "[$");
        o->num_text_start = b->length;
    } else if (!strcmp(name, "text-content")) {
        owf_buf_putc(b, '@');
    } else if (!strcmp(name, "boolean")) {
        owf_buf_puts(b, "\"TRUE\";\"TRUE\";\"FALSE\"");
    } else if (!strcmp(name, "day")) {
        owf_buf_puts(b, is_long ? "dd" : "d");
    } else if (!strcmp(name, "month")) {
        v = ev(e, NS_NUMBER, "textual");
        if (v && !strcmp(v, "true"))
            owf_buf_puts(b, is_long ? "mmmm" : "mmm");
        else
            owf_buf_puts(b, is_long ? "mm" : "m");
    } else if (!strcmp(name, "year")) {
        owf_buf_puts(b, is_long ? "yyyy" : "yy");
    } else if (!strcmp(name, "day-of-week")) {
        owf_buf_puts(b, is_long ? "dddd" : "ddd");
    } else if (!strcmp(name, "hours")) {
        owf_buf_puts(b, n->elapsed ? (is_long ? "[hh]" : "[h]") : is_long ? "hh" : "h");
        n->elapsed = 0;
    } else if (!strcmp(name, "minutes")) {
        owf_buf_puts(b, n->elapsed ? (is_long ? "[mm]" : "[m]") : is_long ? "mm" : "m");
        n->elapsed = 0;
    } else if (!strcmp(name, "seconds")) {
        long d = whole(ev(e, NS_NUMBER, "decimal-places"), 0, 0, 9);
        owf_buf_puts(b, n->elapsed ? (is_long ? "[ss]" : "[s]") : is_long ? "ss" : "s");
        n->elapsed = 0;
        if (d) {
            owf_buf_putc(b, '.');
            put_zeros(b, '0', d);
        }
    } else if (!strcmp(name, "am-pm")) {
        owf_buf_puts(b, "AM/PM");
    } else if (!strcmp(name, "era") || !strcmp(name, "quarter") || !strcmp(name, "week-of-year")) {
        owf_report_add(o->report, OWF_NOTE_APPROX, "Some parts of date formats (eras, quarters, weeks) are not shown");
    }
}

static void numstyle_child(ods *o, const owf_xml_event *e)
{
    ods_numstyle *n = o->num;
    if (!strcmp(e->ns, NS_NUMBER)) {
        number_element(o, e);
    } else if (is(e, NS_STYLE, "text-properties")) {
        static const struct { const char *rgb, *name; } names[8] = {
            { "#000000", "[Black]" }, { "#0000ff", "[Blue]" }, { "#00ffff", "[Cyan]" }, { "#00ff00", "[Green]" },
            { "#ff00ff", "[Magenta]" }, { "#ff0000", "[Red]" }, { "#ffffff", "[White]" }, { "#ffff00", "[Yellow]" } };
        const char *v = ev(e, NS_FO, "color");
        int i;
        for (i = 0; v && i < 8; i++)
            if (owf_same_word(v, names[i].rgb))
                snprintf(n->colour, sizeof n->colour, "%s", names[i].name);
    } else if (is(e, NS_STYLE, "map") && n->nmaps < 4) {
        const char *cond = ev(e, NS_STYLE, "condition"), *apply = ev(e, NS_STYLE, "apply-style-name");
        if (cond && apply) {
            n->maps[n->nmaps].cond = copy(o, cond);
            n->maps[n->nmaps].apply = copy(o, apply);
            if (n->maps[n->nmaps].cond && n->maps[n->nmaps].apply)
                n->nmaps++;
        }
    }
}

/* The end of number:text or number:currency-symbol: its text as Excel writes it. */
static void num_text_end(ods *o)
{
    owf_buf *b = &o->num->code;
    if (o->num_element == 2) {
        owf_buf_putc(b, ']');
    } else if (!b->failed) {   /* the text as a literal: taken out and put back quoted */
        size_t start = o->num_text_start, n = b->length - start;
        char *raw = malloc(n + 1);
        if (!raw) {
            o->failed = 1;
        } else {
            memcpy(raw, b->data + start, n);
            b->length = start;
            put_literal(b, raw, n, o->num->kind);
            free(raw);
        }
    }
    o->num_text = 0;
}

static void free_num(ods_numstyle *n)
{
    int k;
    free(n->name);
    free(n->excel);
    owf_buf_free(&n->code);
    for (k = 0; k < n->nmaps; k++) {
        free(n->maps[k].cond);
        free(n->maps[k].apply);
    }
}

static const ods_numstyle *find_num(const ods *o, const char *name)
{
    int i;
    for (i = o->nnums - 1; name && i >= 0; i--)
        if (!strcmp(o->nums[i].name, name))
            return &o->nums[i];
    return NULL;
}

/* One section: its colour, then its elements' text. */
static void put_section(owf_buf *b, const ods_numstyle *n)
{
    if (!n) {
        owf_buf_puts(b, "General");
        return;
    }
    owf_buf_puts(b, n->colour);
    if (n->general && n->elements == 1)
        owf_buf_puts(b, "General");
    else if (n->code.length)
        owf_buf_put(b, n->code.data, n->code.length);
    else
        owf_buf_puts(b, "General");
}

/* The whole format: libowf's own record when the file has it; else the
 * style and the sections its maps choose, in Excel's order: positive (and
 * zero), negative, zero, text. */
static int numfmt_text(ods *o, const ods_numstyle *n, owf_buf *out)
{
    const ods_numstyle *pos = NULL, *neg = NULL, *zero = NULL, *posz = NULL, *any = NULL;
    owf_buf b = *out;
    int i;
    if (n->excel) {
        owf_buf_puts(&b, n->excel);
        owf_buf_putc(&b, 0);
        *out = b;
        return !b.failed;
    }
    for (i = 0; i < n->nmaps; i++) {
        const char *c = n->maps[i].cond;
        const ods_numstyle *m = find_num(o, n->maps[i].apply);
        if (!strncmp(c, "value()", 7))
            c += 7;
        if (!strcmp(c, ">=0"))
            posz = m;
        else if (!strcmp(c, ">0"))
            pos = m;
        else if (!strcmp(c, "<0"))
            neg = m;
        else if (!strcmp(c, "=0"))
            zero = m;
        else
            any = m;
    }
    if (posz) {
        put_section(&b, posz);
        owf_buf_putc(&b, ';');
        put_section(&b, n);
    } else if (pos && neg) {
        put_section(&b, pos);
        owf_buf_putc(&b, ';');
        put_section(&b, neg);
        owf_buf_putc(&b, ';');
        if (zero) {
            put_section(&b, zero);
            owf_buf_putc(&b, ';');
        }
        put_section(&b, n);
    } else if (any || pos) {
        put_section(&b, any ? any : pos);
        owf_buf_putc(&b, ';');
        put_section(&b, n);
        if (!any || n->kind != N_TEXT)
            owf_report_add(o->report, OWF_NOTE_APPROX, "Number formats with conditions keep their first condition only");
    } else {
        put_section(&b, n);
    }
    owf_buf_putc(&b, 0);
    *out = b;
    return !b.failed;
}

static int numfmt_index(ods *o, const char *name)
{
    const ods_numstyle *n = find_num(o, name);
    owf_buf b;
    int index;
    if (!n)
        return 0;
    owf_buf_init(&b);
    if (!numfmt_text(o, n, &b)) {
        o->failed = 1;
        owf_buf_free(&b);
        return 0;
    }
    index = owf_book_numfmt(o->book, (const char *)b.data);
    owf_buf_free(&b);
    if (index < 0) {
        o->failed = 1;
        return 0;
    }
    return index;
}

/* A cell style through its parents, onto the default's, as the book's
 * style. */
static void apply_style(ods *o, const ods_style *s, owf_cellstyle *c, int depth)
{
    int i;
    if (!s || depth > 32)
        return;
    if (s->parent)
        apply_style(o, find_style(o, s->parent, F_CELL), c, depth + 1);
    if ((s->mask & P_FONT) && s->font) {
        owf_font_kind kind = OWF_FONT_ANY;
        int font;
        if (s->font_kind >= 0)
            kind = (owf_font_kind)s->font_kind;
        else                             /* a family named directly: its kind from the name */
            owf_font_modern(s->font, &kind);
        font = owf_book_font(o->book, s->font, kind);
        if (font < 0)
            o->failed = 1;
        else
            c->font.font = font;
    }
    if (s->mask & P_SIZE)
        c->font.size = s->size;
    c->font.flags = (c->font.flags & ~s->flag_mask) | s->flags;
    if (s->mask & P_COLOUR)
        c->font.colour = s->colour;
    if (s->mask & P_HALIGN)
        c->halign = (unsigned char)s->halign;
    if (s->mask & P_VALIGN)
        c->valign = (unsigned char)s->valign;
    if (s->mask & P_WRAP)
        c->wrap = (unsigned char)s->wrap;
    if (s->mask & P_INDENT)
        c->indent = (unsigned char)s->indent;
    if (s->mask & P_FILL)
        c->fill = s->fill;
    for (i = 0; i < 4; i++)
        if (s->mask & (P_BORDER << i)) {
            c->border[i] = s->border[i];
            c->border_colour[i] = s->border_colour[i];
        }
    if (s->mask & P_DATA)
        c->number_format = numfmt_index(o, s->data_style);
}

static void base_style(ods *o, owf_cellstyle *c)
{
    owf_cellstyle_init(c);
    apply_style(o, default_style(o, F_CELL), c, 0);
    apply_style(o, find_style(o, "Default", F_CELL), c, 0);
}

static int model_style(ods *o, const char *name)
{
    ods_style *s;
    owf_cellstyle c;
    int index;
    if (!name || !strcmp(name, "Default"))
        return 0;
    if (o->last_name && !strcmp(o->last_name, name))
        return o->last_model;
    s = (ods_style *)find_style(o, name, F_CELL);
    if (!s)
        return 0;
    if (s->model < 0) {
        base_style(o, &c);
        apply_style(o, s, &c, 0);
        index = owf_book_style(o->book, &c);
        if (index < 0) {
            o->failed = 1;
            index = 0;
        }
        s->model = index;
    }
    o->last_name = s->name;
    o->last_model = s->model;
    return s->model;
}

/* ---- Cells ---- */

static const char *cell_attr(const ods *o, const char *ns, const char *name)
{
    int i;
    for (i = 0; i < o->ncattrs; i++)
        if (!strcmp(o->cattrs[i].name, name) && !strcmp(o->cattrs[i].ns, ns))
            return o->cattrs[i].value;
    return NULL;
}

static const char *column_cell_style(const ods *o, unsigned long col)
{
    int lo = 0, hi = o->ncols;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (o->cols[mid].last < col)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo < o->ncols && o->cols[lo].first <= col ? o->cols[lo].cell_style : NULL;
}

static void set_text(ods *o, owf_cell *c, const char *s, size_t n)
{
    if (owf_utf8_valid((const unsigned char *)s, n)) {
        if (owf_cell_set_text(c, s, n) < 0)
            o->failed = 1;
    } else {
        owf_buf b;
        owf_buf_init(&b);
        owf_buf_put_utf8_clean(&b, s, n);
        if (b.failed || owf_cell_set_text(c, b.data ? (const char *)b.data : "", b.length) < 0)
            o->failed = 1;
        owf_buf_free(&b);
    }
}

/* The value from the cell's attributes and text; 0 when it has none. */
static int cell_value(ods *o, owf_cell *c)
{
    const char *type = cell_attr(o, NS_OFFICE, "value-type"), *v, *ext = cell_attr(o, NS_CALCEXT, "value-type");
    const char *text = o->text.length ? (const char *)o->text.data : "";
    double d;
    if (ext && !strcmp(ext, "error")) {
        int code = owf_xl_error_code(text, o->text.length);
        if (code < 0) {
            owf_report_add(o->report, OWF_NOTE_APPROX, "Errors the model does not have (Err:502...) became #VALUE!");
            code = OWF_ERROR_VALUE;
        }
        owf_cell_set_error(c, code);
        return 1;
    }
    if (!type || !strcmp(type, "void"))
        return 0;
    if (!strcmp(type, "float") || !strcmp(type, "percentage") || !strcmp(type, "currency")) {
        v = cell_attr(o, NS_OFFICE, "value");
        if (v && owf_number_parse(v, strlen(v), &d)) {
            owf_cell_set_number(c, d);
            return 1;
        }
        owf_report_add(o->report, OWF_NOTE_LOST, "Some cells' numbers could not be read");
        return 0;
    }
    if (!strcmp(type, "date")) {
        v = cell_attr(o, NS_OFFICE, "date-value");
        if (v && owf_parse_iso_date(v, &d)) {
            owf_cell_set_number(c, d - (o->null_date_1904 ? 1462 : 0));
            return 1;
        }
        owf_report_add(o->report, OWF_NOTE_LOST, "Some cells' numbers could not be read");
        return 0;
    }
    if (!strcmp(type, "time")) {
        v = cell_attr(o, NS_OFFICE, "time-value");
        if (v && owf_parse_iso_duration(v, &d)) {
            owf_cell_set_number(c, d);
            return 1;
        }
        owf_report_add(o->report, OWF_NOTE_LOST, "Some cells' numbers could not be read");
        return 0;
    }
    if (!strcmp(type, "boolean")) {
        v = cell_attr(o, NS_OFFICE, "boolean-value");
        owf_cell_set_bool(c, v && (!strcmp(v, "true") || !strcmp(v, "1")));
        return 1;
    }
    if (!strcmp(type, "string")) {
        v = cell_attr(o, NS_OFFICE, "string-value");
        if (v)
            set_text(o, c, v, strlen(v));
        else
            set_text(o, c, text, o->text.length);
        return 1;
    }
    return 0;
}

static void add_array(ods *o, unsigned long row, unsigned long col, long rows, long cols, const char *text)
{
    ods_array *a;
    if (rows <= 1 && cols <= 1)
        return;
    if (o->narrays >= MAX_ARRAYS) {
        owf_report_add(o->report, OWF_NOTE_LOST, "Some array formulas keep only their first cell's formula");
        return;
    }
    if (!(a = grow(o, o->arrays, o->narrays, &o->caparrays, sizeof *a)))
        return;
    o->arrays = a;
    a = &o->arrays[o->narrays];
    a->range.first_row = (unsigned)row;
    a->range.first_col = (unsigned)col;
    a->range.last_row = (unsigned)(row + (unsigned long)rows - 1 < MAX_ROWS ? row + (unsigned long)rows - 1 : MAX_ROWS - 1);
    a->range.last_col = (unsigned)(col + (unsigned long)cols - 1 < MAX_COLS ? col + (unsigned long)cols - 1 : MAX_COLS - 1);
    if ((a->text = copy(o, text)) != NULL)
        o->narrays++;
}

static void end_cell(ods *o)
{
    const char *formula = cell_attr(o, NS_TABLE, "formula"), *name = cell_attr(o, NS_TABLE, "style-name");
    long repeat = whole(cell_attr(o, NS_TABLE, "number-columns-repeated"), 1, 1, 0x7FFFFFFFL);
    long span_c = whole(cell_attr(o, NS_TABLE, "number-columns-spanned"), 1, 1, (long)MAX_COLS);
    long span_r = whole(cell_attr(o, NS_TABLE, "number-rows-spanned"), 1, 1, (long)MAX_ROWS);
    long mat_c = whole(cell_attr(o, NS_TABLE, "number-matrix-columns-spanned"), 1, 1, (long)MAX_COLS);
    long mat_r = whole(cell_attr(o, NS_TABLE, "number-matrix-rows-spanned"), 1, 1, (long)MAX_ROWS);
    int matrix = cell_attr(o, NS_TABLE, "number-matrix-columns-spanned") || cell_attr(o, NS_TABLE, "number-matrix-rows-spanned");
    const char *rule = cell_attr(o, NS_TABLE, "content-validation-name");
    char *excel = NULL;
    int style, k, has, explicit_style, note = o->note_ready;
    owf_cell probe;

    o->in_cell = 0;
    o->note_ready = 0;
    if (o->col >= MAX_COLS || o->row >= MAX_ROWS) {
        o->col += (unsigned long)repeat;
        return;
    }
    if ((unsigned long)repeat > MAX_COLS - o->col)
        repeat = (long)(MAX_COLS - o->col);
    if (rule)
        rule_cell(o, rule, o->col, o->col + (unsigned long)repeat - 1);
    if (note) {                                  /* its comment, on the first of its columns */
        owf_buf_putc(&o->note_text, 0);
        owf_buf_putc(&o->note_author, 0);
        if (!o->note_text.failed && !o->note_author.failed &&
            owf_sheet_set_comment(o->sheet, (unsigned)o->row, (unsigned)o->col,
                                  o->note_author.data[0] ? (const char *)o->note_author.data : NULL,
                                  (const char *)o->note_text.data) < 0)
            o->failed = 1;
    }
    explicit_style = name != NULL;
    if (!name)
        name = o->row_cell_style ? o->row_cell_style : column_cell_style(o, o->col);
    style = model_style(o, name);
    if (formula) {
        excel = owf_of_to_excel(formula);
        if (!excel)
            owf_report_add(o->report, OWF_NOTE_LOST, "Some formulas could not be read; their cells keep their values");
    }
    if (span_c > 1 || span_r > 1) {
        owf_range r;
        r.first_row = (unsigned)o->row;
        r.first_col = (unsigned)o->col;
        r.last_row = (unsigned)(o->row + (unsigned long)span_r - 1 < MAX_ROWS ? o->row + (unsigned long)span_r - 1 : MAX_ROWS - 1);
        r.last_col = (unsigned)(o->col + (unsigned long)span_c - 1 < MAX_COLS ? o->col + (unsigned long)span_c - 1 : MAX_COLS - 1);
        if (owf_sheet_merge(o->sheet, &r) < 0)
            o->failed = 1;
    }
    /* what the cell holds, tried on a cell of our own first */
    memset(&probe, 0, sizeof probe);
    has = cell_value(o, &probe) || excel || o->text.length;
    /* an empty cell with its column's or row's style only is no cell of
     * the model's: the column keeps the style, as in Excel's files */
    if (!has && (!style || !explicit_style)) {
        free(probe.text);
        free(excel);
        o->col += (unsigned long)repeat;
        return;
    }
    for (k = 0; k < repeat && !o->failed; k++) {
        owf_cell *c;
        if (k) {                         /* a repeated cell costs a share of the budget */
            unsigned long *budget = has ? &o->repeat_budget : &o->empty_budget;
            unsigned long bytes = (probe.text ? strlen(probe.text) : 0) + (excel ? strlen(excel) : 0);
            if (!*budget || bytes > o->text_budget) {
                owf_report_add(o->report, OWF_NOTE_LOST, "Some repeated cells were left out: too many of them");
                break;
            }
            (*budget)--;
            o->text_budget -= bytes;
        } else if (!has) {
            if (!o->empty_budget)
                break;
            o->empty_budget--;
        }
        c = owf_sheet_cell(o->sheet, (unsigned)o->row, (unsigned)(o->col + (unsigned long)k), 1);
        if (!c) {
            o->failed = 1;
            break;
        }
        if (!explicit_style && k && !o->row_cell_style)   /* each column's own default */
            c->style = model_style(o, column_cell_style(o, o->col + (unsigned long)k));
        else
            c->style = style;
        c->type = probe.type;
        c->number = probe.number;
        c->error = probe.error;
        if (probe.type == OWF_VALUE_TEXT && probe.text && owf_cell_set_text(c, probe.text, strlen(probe.text)) < 0)
            o->failed = 1;
        if (probe.type == OWF_VALUE_EMPTY && o->text.length && !excel)
            set_text(o, c, (const char *)o->text.data, o->text.length);    /* text with no value type */
        if (excel) {
            if (owf_cell_set_formula(c, excel) < 0)
                o->failed = 1;
            if (matrix)
                c->flags |= OWF_CELL_ARRAY;
        }
    }
    if (excel && (mat_c > 1 || mat_r > 1))
        add_array(o, o->row, o->col, mat_r, mat_c, excel);
    free(probe.text);
    free(excel);
    o->col += (unsigned long)repeat;
}

static void start_cell(ods *o, const owf_xml_event *e)
{
    int i;
    o->in_cell = 1;
    o->ncattrs = 0;
    for (i = 0; i < e->nattrs && o->ncattrs < 32; i++)
        o->cattrs[o->ncattrs++] = e->attrs[i];
    o->text.length = 0;
    o->text.failed = 0;
    o->paras = 0;
    o->in_p = 0;
}

/* ---- Rows, columns, tables ---- */

static void start_table(ods *o, const owf_xml_event *e)
{
    const char *name = ev(e, NS_TABLE, "name"), *v;
    const ods_style *ts = find_style(o, ev(e, NS_TABLE, "style-name"), F_TABLE);
    owf_buf clean;
    owf_buf_init(&clean);
    owf_buf_put_utf8_clean(&clean, name ? name : "", name ? strlen(name) : 0);
    owf_buf_putc(&clean, 0);
    o->sheet = clean.failed ? NULL : owf_book_add_sheet(o->book, (const char *)clean.data);
    owf_buf_free(&clean);
    if (!o->sheet) {
        o->failed = 1;
        return;
    }
    o->table_depth = e->depth;
    o->row = o->col = 0;
    o->ncols = 0;
    o->ncolumns = 0;
    o->narrays = 0;
    o->cf_priority = 0;
    o->cf_kept = 0;
    o->default_width = o->default_height = -1;
    if (ts && !ts->display)
        o->sheet->hidden = 1;
    if ((v = ev(e, NS_TABLE, "protected")) != NULL && !strcmp(v, "true"))
        owf_report_add(o->report, OWF_NOTE_INFO, "Sheet protection is not kept: the sheets open unprotected");
    if (o->ext_ns) {
        long w = whole(ev(e, o->ext_ns, "default-column-width"), -1, 0, 0x7FFF);
        long h = whole(ev(e, o->ext_ns, "default-row-height"), -1, 0, 0x7FFF);
        if (w >= 0)
            o->sheet->default_width = o->default_width = (int)w;
        if (h >= 0)
            o->sheet->default_height = o->default_height = (int)h;
    }
    {
        const ods_style *dc = default_style(o, F_COLUMN), *dr = default_style(o, F_ROW);
        if (o->default_width < 0 && dc && dc->width > 0)
            o->sheet->default_width = dc->width;
        if (o->default_height < 0 && dr && dr->height > 0)
            o->sheet->default_height = dr->height;
    }
}

static void table_column(ods *o, const owf_xml_event *e)
{
    long repeat = whole(ev(e, NS_TABLE, "number-columns-repeated"), 1, 1, 0x7FFFFFFFL);
    const char *style = ev(e, NS_TABLE, "style-name"), *vis = ev(e, NS_TABLE, "visibility"),
               *cell = ev(e, NS_TABLE, "default-cell-style-name");
    const ods_style *s = find_style(o, style, F_COLUMN);
    int hidden = vis && strcmp(vis, "visible") != 0;
    int width = s ? s->width : o->sheet->default_width;
    int cs = model_style(o, cell);
    unsigned long first = o->ncolumns, last;
    ods_colrun *run;
    if (first >= MAX_COLS)
        return;
    last = (unsigned long)repeat > MAX_COLS - first ? MAX_COLS - 1 : first + (unsigned long)repeat - 1;
    o->ncolumns = last + 1;
    if (cell && strcmp(cell, "Default")) {
        if (!(run = grow(o, o->cols, o->ncols, &o->capcols, sizeof *run)))
            return;
        o->cols = run;
        run = &o->cols[o->ncols++];
        run->first = first;
        run->last = last;
        run->cell_style = cell;
    }
    /* a column as the sheet's default (libowf writes them so) is not one to keep */
    if (!s && !hidden && !cs)
        return;
    if (o->default_width >= 0 && width == o->default_width && !hidden && !cs)
        return;
    if (owf_sheet_set_columns(o->sheet, (unsigned)first, (unsigned)last, width, cs, hidden) < 0)
        o->failed = 1;
}

static void start_row(ods *o, const owf_xml_event *e)
{
    const char *vis = ev(e, NS_TABLE, "visibility");
    o->row_repeat = (unsigned long)whole(ev(e, NS_TABLE, "number-rows-repeated"), 1, 1, 0x7FFFFFFFL);
    o->row_style = ev(e, NS_TABLE, "style-name");
    o->row_cell_style = ev(e, NS_TABLE, "default-cell-style-name");
    if (o->row_cell_style && !strcmp(o->row_cell_style, "Default"))
        o->row_cell_style = NULL;
    o->row_hidden = vis && strcmp(vis, "visible") != 0;
    o->col = 0;
    o->row_first_cell = o->sheet ? o->sheet->ncells : 0;
}

static void end_row(ods *o)
{
    const ods_style *s = find_style(o, o->row_style, F_ROW);
    unsigned long n = o->row_repeat, k;
    int custom = s && !s->optimal, i, first = o->row_first_cell, last = o->sheet->ncells;
    int height = s && s->height > 0 ? s->height : o->sheet->default_height;
    if (o->row >= MAX_ROWS) {
        o->nruns = 0;
        return;
    }
    if (n > MAX_ROWS - o->row)
        n = MAX_ROWS - o->row;
    rule_row(o, n);
    if (custom || o->row_hidden) {
        for (k = 0; k < n && !o->failed; k++) {
            if (k && !o->row_budget--) {
                owf_report_add(o->report, OWF_NOTE_LOST, "Some repeated rows' heights were left out: too many of them");
                break;
            }
            if (owf_sheet_set_row(o->sheet, (unsigned)(o->row + k), height, o->row_hidden) < 0)
                o->failed = 1;
        }
    }
    /* a repeated row with cells: the cells again on each */
    if (n > 1 && last > first) {
        for (k = 1; k < n && !o->failed; k++)
            for (i = first; i < last && !o->failed; i++) {
                owf_cell src = o->sheet->cells[i], *c;
                int empty = src.type == OWF_VALUE_EMPTY && !src.formula;
                unsigned long *budget = empty ? &o->empty_budget : &o->repeat_budget;
                unsigned long bytes = (src.text ? strlen(src.text) : 0) + (src.formula ? strlen(src.formula) : 0);
                if (!*budget || bytes > o->text_budget) {
                    owf_report_add(o->report, OWF_NOTE_LOST, "Some repeated cells were left out: too many of them");
                    k = n;
                    break;
                }
                (*budget)--;
                o->text_budget -= bytes;
                c = owf_sheet_cell(o->sheet, (unsigned)(o->row + k), src.col, 1);
                if (!c) {
                    o->failed = 1;
                    break;
                }
                src = o->sheet->cells[i];  /* the array may have moved */
                c->style = src.style;
                c->type = src.type;
                c->number = src.number;
                c->error = src.error;
                c->flags = src.flags;
                if (src.text && owf_cell_set_text(c, src.text, strlen(src.text)) < 0)
                    o->failed = 1;
                if (src.formula && owf_cell_set_formula(c, src.formula) < 0)
                    o->failed = 1;
            }
    }
    o->row += n;
}

/* After a sheet: array formulas on every cell of their ranges, as the
 * model keeps them. */
static void end_table(ods *o)
{
    int i, k;
    for (i = 0; i < o->narrays && !o->failed; i++) {
        const ods_array *a = &o->arrays[i];
        owf_sheet *sh = o->sheet;
        unsigned long area = (unsigned long)(a->range.last_row - a->range.first_row + 1) *
                             (a->range.last_col - a->range.first_col + 1);
        int lo = 0, hi;
        if (area <= o->array_budget) {
            unsigned r, c;
            o->array_budget -= area;
            for (r = a->range.first_row; r <= a->range.last_row && !o->failed; r++)
                for (c = a->range.first_col; c <= a->range.last_col; c++)
                    if (!owf_sheet_cell(sh, r, c, 1)) {
                        o->failed = 1;
                        break;
                    }
        }
        hi = sh->ncells;
        while (lo < hi) {
            int mid = (lo + hi) / 2;
            if (sh->cells[mid].row < a->range.first_row)
                lo = mid + 1;
            else
                hi = mid;
        }
        for (k = lo; k < sh->ncells && sh->cells[k].row <= a->range.last_row && !o->failed; k++) {
            owf_cell *c = &sh->cells[k];
            if (c->col < a->range.first_col || c->col > a->range.last_col || c->formula)
                continue;
            if (owf_cell_set_formula(c, a->text) < 0)
                o->failed = 1;
            c->flags |= OWF_CELL_ARRAY;
        }
    }
    for (i = 0; i < o->narrays; i++)
        free(o->arrays[i].text);
    o->narrays = 0;
    rule_table(o);
    o->sheet = NULL;
    o->table_depth = 0;
}

/* ---- Named ranges and expressions ---- */

static void named(ods *o, const owf_xml_event *e, int expression)
{
    const char *name = ev(e, NS_TABLE, "name");
    const char *v = expression ? ev(e, NS_TABLE, "expression") : ev(e, NS_TABLE, "cell-range-address");
    ods_name *n;
    char *excel;
    if (!name || !v)
        return;
    if (expression) {
        excel = owf_of_to_excel(v);
    } else {
        /* "$Sheet1.$A$1:.$B$2", perhaps several with spaces: a union */
        owf_buf of;
        const char *p = v;
        int first = 1;
        owf_buf_init(&of);
        owf_buf_puts(&of, "of:=");
        while (*p) {
            const char *start;
            int quoted = 0;
            while (*p == ' ')
                p++;
            if (!*p)
                break;
            start = p;
            while (*p && (quoted || *p != ' ')) {
                if (*p == '\'')
                    quoted = !quoted;
                p++;
            }
            if (!first)
                owf_buf_putc(&of, '~');
            owf_buf_putc(&of, '[');
            owf_buf_put(&of, start, (size_t)(p - start));
            owf_buf_putc(&of, ']');
            first = 0;
        }
        owf_buf_putc(&of, 0);
        excel = of.failed ? NULL : owf_of_to_excel((const char *)of.data);
        owf_buf_free(&of);
    }
    if (!excel) {
        owf_report_add(o->report, OWF_NOTE_LOST, "Some named ranges could not be read");
        return;
    }
    if (!(n = grow(o, o->names, o->nnames, &o->capnames, sizeof *n))) {
        free(excel);
        return;
    }
    o->names = n;
    n = &o->names[o->nnames];
    n->name = copy(o, name);
    n->formula = excel;
    n->sheet = o->sheet ? o->book->nsheets - 1 : -1;
    if (n->name)
        o->nnames++;
    else
        free(excel);
}

/* ---- Settings: the active sheet and frozen panes ---- */

static ods_split *split_for(ods *o, const char *sheet);

/* ooo:view-settings: Views, the first view: ActiveTable, and Tables, a
 * map entry for each sheet with its split (2 is frozen) and where. */
static void config_start(ods *o, const owf_xml_event *e)
{
    const char *name = ev(e, NS_CONFIG, "name");
    if (!strcmp(e->name, "config-item-map-indexed") && name && !strcmp(name, "Views") && !o->cfg_done)
        o->cfg_views = e->depth;
    else if (!strcmp(e->name, "config-item-map-entry") && o->cfg_views && !o->cfg_view && !o->cfg_done &&
             e->depth == o->cfg_views + 1)
        o->cfg_view = e->depth;
    else if (!strcmp(e->name, "config-item-map-named") && name && !strcmp(name, "Tables") && o->cfg_view &&
             e->depth == o->cfg_view + 1)
        o->cfg_tables = e->depth;
    else if (!strcmp(e->name, "config-item-map-entry") && o->cfg_tables && e->depth == o->cfg_tables + 1)
        o->cfg_entry = name;
    else if (!strcmp(e->name, "config-item") && o->cfg_view) {
        o->cfg_item = e->depth;
        o->cfg_item_name = name;
        o->cfg_text.length = 0;
        o->cfg_text.failed = 0;
    }
}

static ods_split *split_for(ods *o, const char *sheet)
{
    ods_split *s;
    int i;
    for (i = 0; i < o->nsplits; i++)
        if (!strcmp(o->splits[i].sheet, sheet))
            return &o->splits[i];
    if (!(s = grow(o, o->splits, o->nsplits, &o->capsplits, sizeof *s)))
        return NULL;
    o->splits = s;
    s = &o->splits[o->nsplits];
    memset(s, 0, sizeof *s);
    if (!(s->sheet = copy(o, sheet)))
        return NULL;
    return &o->splits[o->nsplits++];
}

static void config_end(ods *o, const owf_xml_event *e)
{
    if (!strcmp(e->name, "config-item") && o->cfg_item == e->depth) {
        const char *name = o->cfg_item_name, *v;
        owf_buf_putc(&o->cfg_text, 0);
        v = o->cfg_text.failed ? "" : (const char *)o->cfg_text.data;
        o->cfg_item = 0;
        if (!name)
            return;
        if (e->depth == o->cfg_view + 1 && !strcmp(name, "ActiveTable")) {
            free(o->active);
            o->active = copy(o, v);
        } else if (o->cfg_entry && e->depth == o->cfg_tables + 2) {
            ods_split *sp;
            long n = whole(v, -1, 0, 0x7FFFFFFFL);
            int which = !strcmp(name, "HorizontalSplitMode") ? 1 : !strcmp(name, "VerticalSplitMode") ? 2
                        : !strcmp(name, "HorizontalSplitPosition") ? 3 : !strcmp(name, "VerticalSplitPosition") ? 4 : 0;
            if (!which || n < 0 || !(sp = split_for(o, o->cfg_entry)))
                return;
            if (which == 1)
                sp->hmode = (int)(n > 9 ? 9 : n);
            else if (which == 2)
                sp->vmode = (int)(n > 9 ? 9 : n);
            else if (which == 3)
                sp->hpos = n;
            else
                sp->vpos = n;
        }
    } else if (!strcmp(e->name, "config-item-map-entry") && o->cfg_entry && e->depth == o->cfg_tables + 1) {
        o->cfg_entry = NULL;
    } else if (!strcmp(e->name, "config-item-map-named") && o->cfg_tables == e->depth) {
        o->cfg_tables = 0;
    } else if (!strcmp(e->name, "config-item-map-entry") && o->cfg_view == e->depth) {
        o->cfg_view = 0;
        o->cfg_done = 1;                 /* the first view only */
    } else if (!strcmp(e->name, "config-item-map-indexed") && o->cfg_views == e->depth) {
        o->cfg_views = 0;
    }
}

/* ---- Data validation ---- */

static const char *after(const char *s, const char *prefix)
{
    size_t n = strlen(prefix);
    return strncmp(s, prefix, n) ? NULL : s + n;
}

static char *copy_n(ods *o, const char *s, size_t n)
{
    char *out = malloc(n + 1);
    if (!out) {
        o->failed = 1;
        return NULL;
    }
    memcpy(out, s, n);
    out[n] = 0;
    return out;
}

/* The arguments of "(a,b)" at p, copied (b NULL when there is one); where they end, or NULL. */
static const char *rule_args(ods *o, const char *p, char **a, char **b)
{
    int depth = 0, quote = 0;
    const char *start, *comma = NULL;
    *a = *b = NULL;
    while (*p == ' ')
        p++;
    if (*p != '(')
        return NULL;
    start = ++p;
    for (; *p; p++) {
        if (quote) {
            if (*p == '"') {
                if (p[1] == '"')
                    p++;
                else
                    quote = 0;
            }
            continue;
        }
        if (*p == '"')
            quote = 1;
        else if (*p == '(' || *p == '[')
            depth++;
        else if (*p == ']')
            depth--;
        else if (*p == ')') {
            if (!depth)
                break;
            depth--;
        } else if (*p == ',' && !depth && !comma)
            comma = p;
    }
    if (*p != ')')
        return NULL;
    if (comma) {
        *a = copy_n(o, start, (size_t)(comma - start));
        *b = copy_n(o, comma + 1, (size_t)(p - comma - 1));
    } else {
        *a = copy_n(o, start, (size_t)(p - start));
    }
    return p + 1;
}

/* A list's ODF expression as Excel's: "Yes";"No" as "\"Yes,No\"", a range or a name as its formula. */
static char *list_formula(const char *of)
{
    const char *p = of;
    owf_buf b;
    int n = 0;
    owf_buf_init(&b);
    owf_buf_putc(&b, '"');
    while (*p == ' ')
        p++;
    while (*p == '"') {
        p++;
        if (n++)
            owf_buf_putc(&b, ',');
        while (*p && !(*p == '"' && p[1] != '"')) {
            if (*p == '"')
                p++;
            owf_buf_putc(&b, *p++);
        }
        if (*p != '"')
            break;
        p++;
        while (*p == ' ')
            p++;
        if (*p != ';')
            break;
        p++;
        while (*p == ' ')
            p++;
    }
    if (n && !*p && !b.failed) {
        owf_buf_putc(&b, '"');
        owf_buf_putc(&b, 0);
        if (!b.failed)
            return (char *)b.data;
    }
    owf_buf_free(&b);
    return owf_of_to_excel(of);
}

/* A comparison and its value, "<=10": the operator (OWF_CF_...) and the value as Excel's formula. */
static int compared(const char *p, owf_validation *v)
{
    static const struct { const char *text; int op; } ops[] = {
        { "<=", OWF_CF_LESS_EQUAL }, { ">=", OWF_CF_GREATER_EQUAL }, { "!=", OWF_CF_NOT_EQUAL },
        { "<>", OWF_CF_NOT_EQUAL }, { "=", OWF_CF_EQUAL }, { "<", OWF_CF_LESS }, { ">", OWF_CF_GREATER }
    };
    size_t i;
    while (*p == ' ')
        p++;
    for (i = 0; i < sizeof ops / sizeof ops[0]; i++) {
        const char *q = after(p, ops[i].text);
        if (q) {
            v->op = ops[i].op;
            while (*q == ' ')
                q++;
            return *q && (v->formula[0] = owf_of_to_excel(q)) != NULL;
        }
    }
    return 0;
}

/* ODF's table:condition ("of:cell-content-is-whole-number() and cell-content-is-between(1,10)") as the
 * model's rule; 0 when it says something the model has no rule for. */
static int read_condition(ods *o, const char *c, owf_validation *v)
{
    static const struct { const char *text; int type; } kinds[] = {
        { "cell-content-is-whole-number()", OWF_DV_WHOLE }, { "cell-content-is-decimal-number()", OWF_DV_DECIMAL },
        { "cell-content-is-date()", OWF_DV_DATE }, { "cell-content-is-time()", OWF_DV_TIME }
    };
    const char *p, *q;
    char *a = NULL, *b = NULL;
    int ok = 0, two = 0, not_between = 0;
    size_t i;
    if ((q = after(c, "of:")) != NULL || (q = after(c, "oooc:")) != NULL)
        c = q;
    for (p = c; *p == ' '; p++)
        ;
    v->type = OWF_DV_ANY;
    for (i = 0; i < sizeof kinds / sizeof kinds[0]; i++)
        if ((q = after(p, kinds[i].text)) != NULL) {
            v->type = kinds[i].type;
            while (*q == ' ')
                q++;
            if (!*q) {                           /* any number of the kind: Excel's has bounds, as wide as it takes */
                static const char *const lo[] = { "", "-1E+307", "-1E+307", "", "1", "0" },
                                         *hi[] = { "", "1E+307", "1E+307", "", "2958465", "0.999988425925926" };
                v->op = OWF_CF_BETWEEN;
                return (v->formula[0] = copy(o, lo[v->type])) != NULL && (v->formula[1] = copy(o, hi[v->type])) != NULL;
            }
            if (!(p = after(q, "and")))
                return 0;
            while (*p == ' ')
                p++;
            break;
        }
    if ((q = after(p, "cell-content-is-in-list")) != NULL) {
        if (rule_args(o, q, &a, &b) && a && !b) {
            v->type = OWF_DV_LIST;
            ok = (v->formula[0] = list_formula(a)) != NULL;
        }
    } else if ((q = after(p, "is-true-formula")) != NULL) {
        if (rule_args(o, q, &a, &b) && a && !b) {
            v->type = OWF_DV_CUSTOM;
            ok = (v->formula[0] = owf_of_to_excel(a)) != NULL;
        }
    } else if ((q = after(p, "cell-content-text-length-is-between")) != NULL ||
               (not_between = (q = after(p, "cell-content-text-length-is-not-between")) != NULL) != 0) {
        v->type = OWF_DV_TEXT_LENGTH;
        two = 1;
    } else if ((q = after(p, "cell-content-text-length()")) != NULL) {
        v->type = OWF_DV_TEXT_LENGTH;
        ok = compared(q, v);
    } else if ((q = after(p, "cell-content-is-between")) != NULL ||
               (not_between = (q = after(p, "cell-content-is-not-between")) != NULL) != 0) {
        two = 1;
    } else if ((q = after(p, "cell-content()")) != NULL) {
        ok = compared(q, v);
    }
    if (two && rule_args(o, q, &a, &b) && a && b) {
        v->op = not_between ? OWF_CF_NOT_BETWEEN : OWF_CF_BETWEEN;
        ok = (v->formula[0] = owf_of_to_excel(a)) != NULL && (v->formula[1] = owf_of_to_excel(b)) != NULL;
    }
    if (ok && v->type == OWF_DV_ANY)               /* a number with no kind said: any number */
        v->type = OWF_DV_DECIMAL;
    free(a);
    free(b);
    return ok;
}

/* table:content-validation: a rule, its cells to come. */
static void rule_start(ods *o, const owf_xml_event *e)
{
    const char *name = ev(e, NS_TABLE, "name"), *cond = ev(e, NS_TABLE, "condition"), *v;
    ods_rule *r;
    if (!name || !(r = grow(o, o->rules, o->nrules, &o->caprules, sizeof *r)))
        return;
    o->rules = r;
    r = &o->rules[o->nrules];
    memset(r, 0, sizeof *r);
    if (!(r->name = copy(o, name)))
        return;
    o->nrules++;
    o->rule_cur = o->nrules;
    r->v.allow_blank = !(v = ev(e, NS_TABLE, "allow-empty-cell")) || strcmp(v, "false") != 0;
    r->v.no_dropdown = (v = ev(e, NS_TABLE, "display-list")) != NULL && !strcmp(v, "none");
    r->ok = !cond || read_condition(o, cond, &r->v);
    if (!r->ok)
        owf_report_add(o->report, OWF_NOTE_LOST, "Some data validation rules could not be read: their cells take anything");
}

/* Its help-message (which 1) or error-message (2). */
static void rule_message(ods *o, const owf_xml_event *e, int which)
{
    ods_rule *r = &o->rules[o->rule_cur - 1];
    const char *t = ev(e, NS_TABLE, "title"), *d = ev(e, NS_TABLE, "display"), *m = ev(e, NS_TABLE, "message-type");
    int display = !d || !strcmp(d, "true");
    o->rule_msg = which;
    o->rule_text.length = 0;
    o->rule_paras = 0;
    if (which == 1) {
        r->v.show_prompt = display;
        if (t && !r->v.prompt_title)
            r->v.prompt_title = copy(o, t);
    } else {
        r->v.show_error = display;
        r->v.error_style = m && !strcmp(m, "warning") ? OWF_DV_WARNING : m && !strcmp(m, "information") ? OWF_DV_INFORMATION
                                                                                                         : OWF_DV_STOP;
        if (t && !r->v.error_title)
            r->v.error_title = copy(o, t);
    }
}

static void rule_message_end(ods *o)
{
    ods_rule *r = &o->rules[o->rule_cur - 1];
    char **to = o->rule_msg == 1 ? &r->v.prompt : &r->v.error;
    if (o->rule_text.length && !*to && !o->rule_text.failed)
        *to = copy_n(o, (const char *)o->rule_text.data, o->rule_text.length);
    o->rule_msg = 0;
    o->rule_p = 0;
}

/* A cell (repeated to last) naming a rule: one more of its cells in the row being read. */
static void rule_cell(ods *o, const char *name, unsigned long first, unsigned long last)
{
    ods_run *run;
    int i;
    for (i = 0; i < o->nrules && strcmp(o->rules[i].name, name); i++)
        ;
    if (i == o->nrules || !o->rules[i].ok)
        return;
    if (o->nruns && o->runs[o->nruns - 1].rule == i && o->runs[o->nruns - 1].last + 1 == first) {
        o->runs[o->nruns - 1].last = last;
        return;
    }
    if (!(run = grow(o, o->runs, o->nruns, &o->capruns, sizeof *run)))
        return;
    o->runs = run;
    run = &o->runs[o->nruns++];
    run->rule = i;
    run->first = first;
    run->last = last;
}

/* The row's runs (repeated over n rows) into their rules' ranges on the sheet: a range below another of the
 * same columns grows it, as one beside another of the same rows. */
static void rule_row(ods *o, unsigned long n)
{
    int k, i, j;
    for (k = 0; k < o->nruns && !o->failed; k++) {
        const ods_run *run = &o->runs[k];
        ods_use *u;
        owf_range *r;
        unsigned long last_row = o->row + n - 1 < MAX_ROWS ? o->row + n - 1 : MAX_ROWS - 1;
        for (i = 0; i < o->nuses && o->uses[i].rule != run->rule; i++)
            ;
        if (i == o->nuses) {
            if (!(u = grow(o, o->uses, o->nuses, &o->capuses, sizeof *u)))
                return;
            o->uses = u;
            u = &o->uses[o->nuses++];
            memset(u, 0, sizeof *u);
            u->rule = run->rule;
        }
        u = &o->uses[i];
        for (j = u->nranges - 1; j >= 0 && j >= u->nranges - 64; j--) {
            r = &u->ranges[j];
            if (r->first_col == run->first && r->last_col == run->last && (unsigned long)r->last_row + 1 == o->row) {
                r->last_row = (unsigned)last_row;
                break;
            }
        }
        if (j >= 0 && j >= u->nranges - 64)
            continue;
        if (u->nranges) {
            r = &u->ranges[u->nranges - 1];
            if (r->first_row == o->row && r->last_row == last_row && (unsigned long)r->last_col + 1 == run->first) {
                r->last_col = (unsigned)run->last;
                continue;
            }
        }
        if (u->nranges >= 65536) {
            owf_report_add(o->report, OWF_NOTE_LOST, "Some cells' data validation was left out: too many ranges");
            continue;
        }
        if (!(r = grow(o, u->ranges, u->nranges, &u->capranges, sizeof *r)))
            return;
        u->ranges = r;
        r = &u->ranges[u->nranges++];
        r->first_row = (unsigned)o->row;
        r->last_row = (unsigned)last_row;
        r->first_col = (unsigned)run->first;
        r->last_col = (unsigned)run->last;
    }
    o->nruns = 0;
}

static char *copy_or_null(ods *o, const char *s)
{
    return s ? copy(o, s) : NULL;
}

/* After a sheet: its rules, each with the ranges its cells gave, in the order the rules came. */
static void rule_table(ods *o)
{
    int i, k;
    for (i = 1; i < o->nuses; i++)                /* by the rule's place: as written */
        for (k = i; k > 0 && o->uses[k - 1].rule > o->uses[k].rule; k--) {
            ods_use t = o->uses[k];
            o->uses[k] = o->uses[k - 1];
            o->uses[k - 1] = t;
        }
    for (i = 0; i < o->nuses; i++) {
        ods_use *u = &o->uses[i];
        const owf_validation *src = &o->rules[u->rule].v;
        owf_validation *v = o->failed ? NULL : owf_sheet_add_validation(o->sheet);
        if (!v) {
            o->failed = 1;
            free(u->ranges);
            continue;
        }
        *v = *src;
        v->ranges = u->ranges;
        v->nranges = u->nranges;
        v->formula[0] = copy_or_null(o, src->formula[0]);
        v->formula[1] = copy_or_null(o, src->formula[1]);
        v->prompt_title = copy_or_null(o, src->prompt_title);
        v->prompt = copy_or_null(o, src->prompt);
        v->error_title = copy_or_null(o, src->error_title);
        v->error = copy_or_null(o, src->error);
    }
    o->nuses = 0;
    o->nruns = 0;
}

/* ---- AutoFilters ---- */

/* "'My sheet'.$A$6" at *p: the sheet's name (copied into name, when there is one) and the cell; where it
 * ends. 0 when it is not one. */
static int odf_cell(const char **p, char *name, size_t size, unsigned long *row, unsigned long *col)
{
    const char *s = *p;
    size_t n = 0;
    unsigned long c = 0, r = 0;
    if (*s == '$')
        s++;
    if (*s == '\'') {
        for (s++; *s && !(*s == '\'' && s[1] != '\''); s++) {
            if (*s == '\'')
                s++;
            if (n + 1 < size)
                name[n++] = *s;
        }
        if (*s != '\'')
            return 0;
        s++;
        name[n] = 0;
    } else if (*s != '.') {
        const char *dot = strchr(s, '.');
        if (!dot)
            return 0;
        for (; s < dot; s++)
            if (n + 1 < size)
                name[n++] = *s;
        name[n] = 0;
    }
    if (*s++ != '.')
        return 0;
    if (*s == '$')
        s++;
    while ((*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z')) {
        c = c * 26 + (unsigned long)((*s & 0x1F));
        s++;
        if (c > MAX_COLS)
            return 0;
    }
    if (*s == '$')
        s++;
    while (*s >= '0' && *s <= '9') {
        r = r * 10 + (unsigned long)(*s - '0');
        s++;
        if (r > MAX_ROWS)
            return 0;
    }
    if (!c || !r)
        return 0;
    *row = r - 1;
    *col = c - 1;
    *p = s;
    return 1;
}

/* table:database-range: with its buttons, an AutoFilter on its sheet. */
static void filter_start(ods *o, const owf_xml_event *e)
{
    const char *v = ev(e, NS_TABLE, "display-filter-buttons"), *p = ev(e, NS_TABLE, "target-range-address");
    char name[256] = "", second[256] = "";
    unsigned long r0, c0, r1, c1;
    int i;
    o->af_sheet = NULL;
    if (!v || strcmp(v, "true") || !p || !odf_cell(&p, name, sizeof name, &r0, &c0) || *p++ != ':' ||
        !odf_cell(&p, second, sizeof second, &r1, &c1) || r1 < r0 || c1 < c0)
        return;
    for (i = 0; i < o->book->nsheets && strcmp(o->book->sheets[i].name, name); i++)
        ;
    if (i == o->book->nsheets || o->book->sheets[i].has_autofilter)
        return;
    o->af_sheet = &o->book->sheets[i];
    o->af_sheet->has_autofilter = 1;
    o->af_sheet->autofilter.first_row = (unsigned)r0;
    o->af_sheet->autofilter.first_col = (unsigned)c0;
    o->af_sheet->autofilter.last_row = (unsigned)r1;
    o->af_sheet->autofilter.last_col = (unsigned)c1;
    o->af_xml.length = 0;
    o->af_xml.failed = 0;
    o->af_field = -1;
    o->af_or = o->af_approx = 0;
}

/* The XML of a filterColumn's value: as escaped in an attribute. */
static void af_value(ods *o, const char *v)
{
    owf_buf_puts(&o->af_xml, "<filter val=\"");
    for (; *v; v++) {
        switch (*v) {
        case '&': owf_buf_puts(&o->af_xml, "&amp;"); break;
        case '<': owf_buf_puts(&o->af_xml, "&lt;"); break;
        case '>': owf_buf_puts(&o->af_xml, "&gt;"); break;
        case '"': owf_buf_puts(&o->af_xml, "&quot;"); break;
        default: owf_buf_putc(&o->af_xml, *v);
        }
    }
    owf_buf_puts(&o->af_xml, "\"/>");
}

/* Inside it: table:filter-or, table:filter-condition (a field equal to a value, or to one of a set). */
static void filter_child(ods *o, const owf_xml_event *e)
{
    if (is(e, NS_TABLE, "filter-or")) {
        o->af_or = 1;
    } else if (is(e, NS_TABLE, "filter-condition")) {
        const char *op = ev(e, NS_TABLE, "operator"), *val = ev(e, NS_TABLE, "value");
        long field = whole(ev(e, NS_TABLE, "field-number"), -1, 0, (long)MAX_COLS);
        if (o->af_field >= 0)
            owf_buf_puts(&o->af_xml, "</filters></filterColumn>");
        o->af_field = -1;
        if (!op || strcmp(op, "=") || field < 0 || !val) {
            o->af_approx = 1;
            return;
        }
        o->af_field = (int)field;
        o->af_values = 0;
        owf_buf_printf(&o->af_xml, "<filterColumn colId=\"%ld\"><filters>", field);
        free(o->af_value);
        o->af_value = copy(o, val);          /* one value, unless a set's items say them all */
    } else if (is(e, NS_TABLE, "filter-set-item") && o->af_field >= 0) {
        const char *val = ev(e, NS_TABLE, "value");
        if (val) {
            af_value(o, val);
            o->af_values++;
        }
    }
}

/* The end of a filter-condition: its own value when no set's items said any. */
static void filter_condition_end(ods *o)
{
    if (o->af_field >= 0 && !o->af_values && o->af_value) {
        af_value(o, o->af_value);
        o->af_values = 1;
    }
    free(o->af_value);
    o->af_value = NULL;
}

static void filter_end(ods *o)
{
    owf_sheet *sh = o->af_sheet;
    if (o->af_field >= 0)
        owf_buf_puts(&o->af_xml, "</filters></filterColumn>");
    o->af_sheet = NULL;
    if (o->af_or || o->af_approx)
        owf_report_add(o->report, OWF_NOTE_APPROX, "Some AutoFilters' conditions (other than a column's values) are left out: their buttons are kept");
    if (o->af_or || !sh || !o->af_xml.length || o->af_xml.failed)
        return;
    if (!(sh->autofilter_xml = copy_n(o, (const char *)o->af_xml.data, o->af_xml.length)))
        return;
}

/* ---- Conditional formatting ---- */

/* An ODF text in a condition ("Review", its quotes doubled) at p: copied unquoted into out; where it ends. */
static const char *of_string(const char *p, char *out, size_t size)
{
    size_t k = 0;
    while (*p == ' ')
        p++;
    if (*p++ != '"')
        return NULL;
    for (; *p && !(*p == '"' && p[1] != '"'); p++) {
        if (*p == '"')
            p++;
        if (k + 1 < size)
            out[k++] = *p;
    }
    out[k] = 0;
    return *p == '"' ? p + 1 : NULL;
}

/* Text for XML: an attribute's (quotes escaped too) or an element's. */
static void put_escaped(owf_buf *b, const char *s, int attribute)
{
    for (; *s; s++)
        switch (*s) {
        case '&': owf_buf_puts(b, "&amp;"); break;
        case '<': owf_buf_puts(b, "&lt;"); break;
        case '>': owf_buf_puts(b, "&gt;"); break;
        case '"':
            owf_buf_puts(b, attribute ? "&quot;" : "\"");
            break;
        default: owf_buf_putc(b, *s);
        }
}

static void put_escaped_xml(owf_buf *b, const char *s)
{
    put_escaped(b, s, 1);
}

/* A cell's name, A1, for Excel's formulas. */
static void put_cell_name(owf_buf *b, unsigned row, unsigned col)
{
    char letters[8];
    int n = 0;
    unsigned c = col + 1;
    while (c && n < 7) {
        letters[n++] = (char)('A' + (c - 1) % 26);
        c = (c - 1) / 26;
    }
    while (n)
        owf_buf_putc(b, letters[--n]);
    owf_buf_printf(b, "%u", row + 1);
}

/* calcext:conditional-format: its ranges ("Sheet1.A1:Sheet1.B9 Sheet1.D2"), for the rules inside it. */
static void cf_format(ods *o, const owf_xml_event *e)
{
    const char *p = ev(e, NS_CALCEXT, "target-range-address");
    char name[256];
    o->cf_nranges = 0;
    while (p && *p) {
        unsigned long r0, c0, r1, c1;
        owf_range *r;
        while (*p == ' ')
            p++;
        if (!*p || !odf_cell(&p, name, sizeof name, &r0, &c0))
            break;
        r1 = r0;
        c1 = c0;
        if (*p == ':' && (p++, !odf_cell(&p, name, sizeof name, &r1, &c1)))
            break;
        if (r1 < r0 || c1 < c0)
            continue;
        if (!(r = grow(o, o->cf_ranges, o->cf_nranges, &o->cf_capranges, sizeof *r)))
            return;
        o->cf_ranges = r;
        r = &o->cf_ranges[o->cf_nranges++];
        r->first_row = (unsigned)r0;
        r->first_col = (unsigned)c0;
        r->last_row = (unsigned)r1;
        r->last_col = (unsigned)c1;
    }
}

/* A new rule over the format's ranges, its priority libowf's own (when it wrote the file) or the next. */
static owf_condfmt *cf_new(ods *o, const owf_xml_event *e)
{
    owf_condfmt *cf;
    long priority = o->ext_ns ? whole(ev(e, o->ext_ns, "priority"), 0, 1, 0x7FFFFFFFL) : 0;
    const char *stop = o->ext_ns ? ev(e, o->ext_ns, "stop-if-true") : NULL;
    if (!o->cf_nranges)
        return NULL;
    if (!(cf = owf_sheet_add_condfmt(o->sheet))) {
        o->failed = 1;
        return NULL;
    }
    if (!(cf->ranges = malloc((size_t)o->cf_nranges * sizeof *cf->ranges))) {
        o->failed = 1;
        return NULL;
    }
    memcpy(cf->ranges, o->cf_ranges, (size_t)o->cf_nranges * sizeof *cf->ranges);
    cf->nranges = o->cf_nranges;
    cf->priority = priority > 0 ? (int)priority : o->cf_priority + 1;
    if (cf->priority > o->cf_priority)
        o->cf_priority = cf->priority;
    cf->stop = stop && !strcmp(stop, "true");
    return cf;
}

/* A kept rule's XML, as Excel's cfRule (for writing back as .xlsx), with the formula its kind holds by. */
static void cf_keep(ods *o, owf_condfmt *cf, const char *start, const char *formula)
{
    owf_buf b;
    owf_buf_init(&b);
    owf_buf_puts(&b, start);
    if (formula) {
        owf_buf_puts(&b, "><formula>");
        put_escaped(&b, formula, 0);
        owf_buf_puts(&b, "</formula></cfRule>");
    } else {
        owf_buf_puts(&b, "/>");
    }
    owf_buf_putc(&b, 0);
    cf->type = OWF_CF_KEPT;
    if (b.failed || (formula && !(cf->formula[0] = copy(o, formula)))) {
        owf_buf_free(&b);
        o->failed = 1;
        return;
    }
    cf->xml = (char *)b.data;
}

/* calcext:condition: a rule whose look is its style's, held when its value says. */
static void cf_condition(ods *o, const owf_xml_event *e)
{
    static const struct { const char *text; int op; } ops[] = {
        { "<=", OWF_CF_LESS_EQUAL }, { ">=", OWF_CF_GREATER_EQUAL }, { "!=", OWF_CF_NOT_EQUAL }, { "<>", OWF_CF_NOT_EQUAL },
        { "=", OWF_CF_EQUAL }, { "<", OWF_CF_LESS }, { ">", OWF_CF_GREATER }
    };
    static const struct { const char *odf, *xl, *op; } texts[] = {
        { "contains-text", "containsText", "containsText" }, { "not-contains-text", "notContainsText", "notContains" },
        { "begins-with", "beginsWith", "beginsWith" }, { "ends-with", "endsWith", "endsWith" }
    };
    const char *v = ev(e, NS_CALCEXT, "value"), *q;
    const char *look = ev(e, NS_CALCEXT, "apply-style-name");
    const ods_style *st = find_style(o, look, F_CELL);
    owf_condfmt *cf;
    char *a = NULL, *b = NULL, text[512];
    int ok = 0;
    size_t i;
    if (!v || !(cf = cf_new(o, e)))
        return;
    if (!st && look) {                               /* LibreOffice names the style by its display name: its name */
        owf_buf n;                                   /* has "_" as "_5f_" and a space as "_20_" */
        owf_buf_init(&n);
        for (q = look; *q; q++) {
            if (*q == '_')
                owf_buf_puts(&n, "_5f_");
            else if (*q == ' ')
                owf_buf_puts(&n, "_20_");
            else
                owf_buf_putc(&n, *q);
        }
        owf_buf_putc(&n, 0);
        if (!n.failed)
            st = find_style(o, (const char *)n.data, F_CELL);
        owf_buf_free(&n);
    }
    if (st) {                                        /* its look: what its style sets */
        if (st->mask & P_FILL)
            cf->fill = st->fill;
        if ((st->mask & P_COLOUR) && st->colour != OWF_COLOUR_AUTO)
            cf->font_colour = OWF_SHADE(st->colour);
        cf->font_set = st->flag_mask & (OWF_BOLD | OWF_ITALIC | OWF_UNDERLINE | OWF_STRIKE);
        cf->font_flags = st->flags & cf->font_set;
    }
    while (*v == ' ')
        v++;
    if ((q = after(v, "between")) != NULL || (q = after(v, "not-between")) != NULL) {
        cf->type = OWF_CF_CELL_IS;
        cf->op = *v == 'n' ? OWF_CF_NOT_BETWEEN : OWF_CF_BETWEEN;
        ok = rule_args(o, q, &a, &b) && a && b && (cf->formula[0] = owf_of_to_excel(a)) != NULL &&
             (cf->formula[1] = owf_of_to_excel(b)) != NULL;
    } else if ((q = after(v, "formula-is")) != NULL) {
        cf->type = OWF_CF_EXPRESSION;
        ok = rule_args(o, q, &a, &b) && a && !b && (cf->formula[0] = owf_of_to_excel(a)) != NULL;
    } else {
        for (i = 0; i < sizeof texts / sizeof texts[0] && !ok; i++)
            if ((q = after(v, texts[i].odf)) != NULL && *q == '(' && of_string(q + 1, text, sizeof text)) {
                owf_buf f, start;
                owf_buf_init(&f);
                owf_buf_init(&start);
                owf_buf_putc(&f, 0);                 /* the cell: the ranges' first, as Excel's formula has it */
                f.length = 0;
                {
                    owf_buf cell;
                    owf_buf_init(&cell);
                    put_cell_name(&cell, cf->ranges[0].first_row, cf->ranges[0].first_col);
                    owf_buf_putc(&cell, 0);
                    owf_buf_puts(&f, i == 0 ? "NOT(ISERROR(SEARCH(\"" : i == 1 ? "ISERROR(SEARCH(\"" : i == 2 ? "LEFT(" : "RIGHT(");
                    if (i < 2) {
                        const char *t;
                        for (t = text; *t; t++) {
                            if (*t == '"')
                                owf_buf_putc(&f, '"');
                            owf_buf_putc(&f, *t);
                        }
                        owf_buf_printf(&f, "\",%s))%s", cell.failed ? "A1" : (const char *)cell.data, i == 0 ? ")" : "");
                    } else {
                        const char *t;
                        owf_buf_printf(&f, "%s,LEN(\"", cell.failed ? "A1" : (const char *)cell.data);
                        for (t = text; *t; t++) {
                            if (*t == '"')
                                owf_buf_putc(&f, '"');
                            owf_buf_putc(&f, *t);
                        }
                        owf_buf_puts(&f, "\"))=\"");
                        for (t = text; *t; t++) {
                            if (*t == '"')
                                owf_buf_putc(&f, '"');
                            owf_buf_putc(&f, *t);
                        }
                        owf_buf_putc(&f, '"');
                    }
                    owf_buf_free(&cell);
                }
                owf_buf_putc(&f, 0);
                owf_buf_printf(&start, "<cfRule type=\"%s\" operator=\"%s\" text=\"", texts[i].xl, texts[i].op);
                put_escaped_xml(&start, text);
                owf_buf_putc(&start, '"');
                owf_buf_putc(&start, 0);
                if (!f.failed && !start.failed) {
                    cf_keep(o, cf, (const char *)start.data, (const char *)f.data);
                    ok = cf->xml != NULL;
                }
                owf_buf_free(&f);
                owf_buf_free(&start);
            }
        if (!ok) {
            static const struct { const char *odf, *xl; } kinds[] = {
                { "duplicate", "<cfRule type=\"duplicateValues\"" }, { "unique", "<cfRule type=\"uniqueValues\"" },
                { "above-equal-average", "<cfRule type=\"aboveAverage\" equalAverage=\"1\"" },
                { "below-equal-average", "<cfRule type=\"aboveAverage\" aboveAverage=\"0\" equalAverage=\"1\"" },
                { "above-average", "<cfRule type=\"aboveAverage\"" },
                { "below-average", "<cfRule type=\"aboveAverage\" aboveAverage=\"0\"" }
            };
            static const struct { const char *odf, *xl; } ranks[] = {
                { "top-elements", "" }, { "bottom-elements", " bottom=\"1\"" }, { "top-percent", " percent=\"1\"" },
                { "bottom-percent", " percent=\"1\" bottom=\"1\"" }
            };
            for (i = 0; i < sizeof kinds / sizeof kinds[0] && !ok; i++)
                if (!strcmp(v, kinds[i].odf)) {
                    cf_keep(o, cf, kinds[i].xl, NULL);
                    ok = cf->xml != NULL;
                }
            if (!ok && (!strcmp(v, "is-error") || !strcmp(v, "is-no-error"))) {
                owf_buf f;                           /* Excel's holds by ISERROR of its first cell */
                owf_buf_init(&f);
                owf_buf_puts(&f, v[3] == 'e' ? "ISERROR(" : "NOT(ISERROR(");
                put_cell_name(&f, cf->ranges[0].first_row, cf->ranges[0].first_col);
                owf_buf_puts(&f, v[3] == 'e' ? ")" : "))");
                owf_buf_putc(&f, 0);
                if (!f.failed)
                    cf_keep(o, cf, v[3] == 'e' ? "<cfRule type=\"containsErrors\"" : "<cfRule type=\"notContainsErrors\"",
                            (const char *)f.data);
                ok = cf->xml != NULL;
                owf_buf_free(&f);
            }
            for (i = 0; i < sizeof ranks / sizeof ranks[0] && !ok; i++)
                if ((q = after(v, ranks[i].odf)) != NULL && rule_args(o, q, &a, &b) && a && !b) {
                    char start[96];
                    snprintf(start, sizeof start, "<cfRule type=\"top10\" rank=\"%ld\"%s", strtol(a, NULL, 10), ranks[i].xl);
                    cf_keep(o, cf, start, NULL);
                    ok = cf->xml != NULL;
                }
        }
        for (i = 0; i < sizeof ops / sizeof ops[0] && !ok && !cf->xml; i++)
            if ((q = after(v, ops[i].text)) != NULL) {
                while (*q == ' ')
                    q++;
                cf->type = OWF_CF_CELL_IS;
                cf->op = ops[i].op;
                ok = *q && (cf->formula[0] = owf_of_to_excel(q)) != NULL;
                break;
            }
    }
    free(a);
    free(b);
    if (!ok) {                                       /* not one the model has: taken back */
        owf_condfmt_free(cf);
        o->sheet->ncondfmts--;
        owf_report_add(o->report, OWF_NOTE_LOST, "Some conditional formatting rules could not be read");
    }
}

/* calcext:color-scale-entry, calcext:formatting-entry: an Excel kind of point. */
static int cf_kind(const char *type)
{
    static const char *const kinds[] = { "minimum", "maximum", "number", "percent", "percentile", "formula" };
    int k;
    for (k = 0; type && k < 6; k++)
        if (!strcmp(type, kinds[k]))
            return k;
    if (type && !strcmp(type, "auto-minimum"))
        return OWF_CFV_MIN;
    if (type && !strcmp(type, "auto-maximum"))
        return OWF_CFV_MAX;
    return OWF_CFV_NUMBER;
}

/* Inside a sheet's calcext:conditional-formats. */
static void cf_start(ods *o, const owf_xml_event *e)
{
    owf_condfmt *cf;
    o->cf_seen = 1;
    if (!strcmp(e->name, "conditional-format")) {
        cf_format(o, e);
    } else if (!strcmp(e->name, "condition")) {
        cf_condition(o, e);
    } else if (!strcmp(e->name, "color-scale")) {
        if ((cf = cf_new(o, e)) != NULL)
            cf->type = OWF_CF_COLOUR_SCALE;
    } else if (!strcmp(e->name, "color-scale-entry") && o->sheet->ncondfmts) {
        unsigned long rgb = 0;
        owf_cfpoint *pt;
        const char *v = ev(e, NS_CALCEXT, "value");
        cf = &o->sheet->condfmts[o->sheet->ncondfmts - 1];
        if (cf->type != OWF_CF_COLOUR_SCALE || cf->npoints >= 3)
            return;
        pt = &cf->points[cf->npoints++];
        pt->kind = cf_kind(ev(e, NS_CALCEXT, "type"));
        if (owf_parse_colour(ev(e, NS_CALCEXT, "color"), &rgb))
            pt->colour = OWF_SHADE(rgb);
        if (pt->kind == OWF_CFV_FORMULA)
            pt->value = v ? owf_of_to_excel(v) : NULL;
        else if (pt->kind != OWF_CFV_MIN && pt->kind != OWF_CFV_MAX)
            pt->value = copy(o, v ? v : "0");
        else
            pt->value = copy(o, "");
    } else if (!strcmp(e->name, "data-bar") || !strcmp(e->name, "icon-set")) {
        unsigned long rgb = 0;
        if (!(cf = cf_new(o, e)))
            return;
        cf->type = OWF_CF_KEPT;
        o->cf_kept = o->sheet->ncondfmts;
        o->cf_xml.length = 0;
        o->cf_xml.failed = 0;
        if (e->name[0] == 'd') {
            const char *lo = ev(e, NS_CALCEXT, "min-length"), *hi = ev(e, NS_CALCEXT, "max-length");
            owf_buf_puts(&o->cf_xml, "<cfRule type=\"dataBar\"><dataBar");
            if (lo && strcmp(lo, "10"))
                owf_buf_printf(&o->cf_xml, " minLength=\"%ld\"", whole(lo, 10, 0, 100));
            if (hi && strcmp(hi, "90"))
                owf_buf_printf(&o->cf_xml, " maxLength=\"%ld\"", whole(hi, 90, 0, 100));
            owf_buf_putc(&o->cf_xml, '>');
            if (!owf_parse_colour(ev(e, NS_CALCEXT, "positive-color"), &rgb))
                rgb = 0x638EC6;
            cf->points[0].colour = OWF_SHADE(rgb);   /* kept for its end */
        } else {
            const char *set = ev(e, NS_CALCEXT, "icon-set-type"), *show = ev(e, NS_CALCEXT, "show-value");
            owf_buf_puts(&o->cf_xml, "<cfRule type=\"iconSet\"><iconSet");
            if (set && strcmp(set, "3TrafficLights1")) {
                owf_buf_puts(&o->cf_xml, " iconSet=\"");
                put_escaped_xml(&o->cf_xml, set);
                owf_buf_putc(&o->cf_xml, '"');
            }
            if (show && !strcmp(show, "false"))
                owf_buf_puts(&o->cf_xml, " showValue=\"0\"");
            owf_buf_putc(&o->cf_xml, '>');
        }
    } else if (!strcmp(e->name, "formatting-entry") && o->cf_kept) {
        static const char *const kinds[] = { "min", "max", "num", "percent", "percentile", "formula" };
        const char *v = ev(e, NS_CALCEXT, "value");
        int k = cf_kind(ev(e, NS_CALCEXT, "type"));
        owf_buf_printf(&o->cf_xml, "<cfvo type=\"%s\"", kinds[k]);
        if (k != OWF_CFV_MIN && k != OWF_CFV_MAX && v) {
            char *f = k == OWF_CFV_FORMULA ? owf_of_to_excel(v) : NULL;
            owf_buf_puts(&o->cf_xml, " val=\"");
            put_escaped_xml(&o->cf_xml, f ? f : v);
            owf_buf_putc(&o->cf_xml, '"');
            free(f);
        }
        owf_buf_puts(&o->cf_xml, "/>");
    } else if (!strcmp(e->name, "date-is")) {
        owf_report_add(o->report, OWF_NOTE_LOST, "Some conditional formatting rules could not be read");
    }
}

static void cf_end(ods *o, const owf_xml_event *e)
{
    owf_condfmt *cf;
    if (!o->cf_kept || (strcmp(e->name, "data-bar") && strcmp(e->name, "icon-set")))
        return;
    cf = &o->sheet->condfmts[o->cf_kept - 1];
    if (e->name[0] == 'd') {
        owf_buf_printf(&o->cf_xml, "<color rgb=\"FF%06lX\"/></dataBar></cfRule>", cf->points[0].colour & 0xFFFFFFUL);
        cf->points[0].colour = 0;
    } else {
        owf_buf_puts(&o->cf_xml, "</iconSet></cfRule>");
    }
    owf_buf_putc(&o->cf_xml, 0);
    if (!o->cf_xml.failed)
        cf->xml = copy(o, (const char *)o->cf_xml.data);
    o->cf_kept = 0;
}

/* ---- Charts: a frame's chart document (an object of the package, or inside the frame in a flat file) ---- */

/* All the text inside a node, in order. */
static void all_text_of(const owf_xml_node *n, owf_buf *b)
{
    const owf_xml_node *k;
    for (k = n ? n->first : NULL; k; k = k->next) {
        if (k->type == OWF_XML_TEXT)
            owf_buf_put(b, k->name, k->length);
        else
            all_text_of(k, b);
    }
}

/* A table cell's text: its paragraphs' (not a description LibreOffice puts beside them). */
static void cell_text_of(const owf_xml_node *cell, owf_buf *b)
{
    const owf_xml_node *p;
    int first = 1;
    for (p = owf_xml_child(cell, NS_TEXT, "p"); p; p = owf_xml_next(p, NS_TEXT, "p")) {
        if (!first)
            owf_buf_putc(b, '\n');
        first = 0;
        all_text_of(p, b);
    }
}

/* A column's (row's) width in twips, as the sheet has it. */
static long sheet_line(const owf_sheet *s, int axis, unsigned i)
{
    int k;
    if (axis) {
        for (k = s->ncols - 1; k >= 0; k--)
            if (i >= s->cols[k].first && i <= s->cols[k].last)
                return s->cols[k].hidden ? 0 : s->cols[k].width > 0 ? s->cols[k].width : s->default_width;
        return s->default_width;
    }
    for (k = 0; k < s->nrows; k++)
        if (s->rows[k].row == i)
            return s->rows[k].hidden ? 0 : s->rows[k].height > 0 ? s->rows[k].height : s->default_height;
    return s->default_height;
}

/* A length ("15.000cm", "2in", "12pt") in EMU, to the EMU. */
static long emu_of(const char *v)
{
    static const struct { const char *unit; double emu; } units[] = {
        { "cm", 360000.0 }, { "mm", 36000.0 }, { "in", 914400.0 }, { "pt", 12700.0 }, { "pc", 152400.0 }, { "px", 9525.0 }
    };
    char *end;
    double d;
    size_t i;
    if (!v)
        return 0;
    d = strtod(v, &end);
    for (i = 0; i < sizeof units / sizeof units[0]; i++)
        if (!strcmp(end, units[i].unit)) {
            d *= units[i].emu;
            return d < 0 ? 0 : d > 2.0e9 ? 2000000000L : (long)(d + 0.5);
        }
    return 0;
}

/* draw:frame: where its chart goes (its cell, or the sheet's corner in table:shapes) and how big it is. */
static void frame_start(ods *o, const owf_xml_event *e, int in_cell)
{
    ods_frame *f;
    const char *end = ev(e, NS_TABLE, "end-cell-address");
    char name[256];
    unsigned long r, c;
    o->frame_depth = e->depth;
    o->frame_ok = 0;
    if (!o->sheet || !(f = grow(o, o->frames, o->nframes, &o->capframes, sizeof *f)))
        return;
    o->frames = f;
    f = &o->frames[o->nframes];
    memset(f, 0, sizeof *f);
    f->sheet = o->book->nsheets - 1;
    f->absolute = !in_cell;
    f->row = in_cell ? (unsigned)(o->row < MAX_ROWS ? o->row : MAX_ROWS - 1) : 0;
    f->col = in_cell ? (unsigned)(o->col < MAX_COLS ? o->col : MAX_COLS - 1) : 0;
    f->z = whole(ev(e, NS_DRAW, "z-index"), o->nframes, 0, 0x7FFFFFFFL);
    f->x = emu_of(ev(e, NS_SVG, "x"));
    f->y = emu_of(ev(e, NS_SVG, "y"));
    f->width = emu_of(ev(e, NS_SVG, "width"));
    f->height = emu_of(ev(e, NS_SVG, "height"));
    if (end && odf_cell(&end, name, sizeof name, &r, &c)) {
        f->has_end = 1;
        f->end_row = (unsigned)r;
        f->end_col = (unsigned)c;
        f->end_x = emu_of(ev(e, NS_TABLE, "end-x"));
        f->end_y = emu_of(ev(e, NS_TABLE, "end-y"));
    }
    o->nframes++;
    o->frame_ok = 1;
}

/* Inside a frame: its object (a chart's, by its place in the package, or its document inline); a picture is not
 * read yet. */
static void frame_child(ods *o, const owf_xml_event *e)
{
    ods_frame *f = o->frame_ok ? &o->frames[o->nframes - 1] : NULL;
    if (is(e, NS_DRAW, "object") && f) {
        const char *href = ev(e, NS_XLINK, "href");
        if (href && *href) {
            size_t n;
            if (!strncmp(href, "./", 2))
                href += 2;
            n = strlen(href);
            while (n && href[n - 1] == '/')
                n--;
            f->object = copy_n(o, href, n);
        }
    } else if (is(e, NS_OFFICE, "document") && f && !f->object) {
        o->inline_depth = e->depth;              /* a flat file's chart: copied as XML, read once the frame ends */
        o->inline_xml.length = 0;
        o->inline_xml.failed = 0;
        inline_start(o, e);
    } else if (is(e, NS_DRAW, "image") && f && !f->object) {
        owf_report_add(o->report, OWF_NOTE_LOST, "Pictures on sheets are not read yet");
        o->frame_ok = 0;
    }
}

/* An element of an inline chart document, copied as XML (each element naming its namespaces). */
static void inline_start(ods *o, const owf_xml_event *e)
{
    owf_buf *b = &o->inline_xml;
    int i;
    owf_buf_printf(b, "<n:%s xmlns:n=\"", e->name);
    put_escaped(b, e->ns, 1);
    owf_buf_putc(b, '"');
    for (i = 0; i < e->nattrs; i++) {
        const owf_xml_attribute *a = &e->attrs[i];
        if (!a->ns || !*a->ns) {
            owf_buf_printf(b, " %s=\"", a->name);
        } else {
            owf_buf_printf(b, " xmlns:a%d=\"", i);
            put_escaped(b, a->ns, 1);
            owf_buf_printf(b, "\" a%d:%s=\"", i, a->name);
        }
        put_escaped(b, a->value ? a->value : "", 1);
        owf_buf_putc(b, '"');
    }
    owf_buf_putc(b, '>');                       /* an empty element's end comes as an end too */
}

static void inline_end(ods *o, const owf_xml_event *e)
{
    owf_buf_printf(&o->inline_xml, "</n:%s>", e->name);
    if (e->depth == o->inline_depth) {
        ods_frame *f = o->frame_ok ? &o->frames[o->nframes - 1] : NULL;
        o->inline_depth = 0;
        owf_buf_putc(&o->inline_xml, 0);
        if (f && !o->inline_xml.failed)
            f->xml = copy(o, (const char *)o->inline_xml.data);
    }
}

/* The chart document's automatic style by name: its element of properties (chart-properties...), or NULL. */
static const owf_xml_node *chart_props(const owf_xml_node *styles, const char *name, const char *which)
{
    const owf_xml_node *st;
    if (!name || !styles)
        return NULL;
    for (st = owf_xml_child(styles, NS_STYLE, "style"); st; st = owf_xml_next(st, NS_STYLE, "style")) {
        const char *n = owf_xml_attr(st, NS_STYLE, "name");
        if (n && !strcmp(n, name))
            return owf_xml_child(st, NS_STYLE, which);
    }
    return NULL;
}

static const char *prop(const owf_xml_node *props, const char *ns, const char *name)
{
    return props ? owf_xml_attr(props, ns, name) : NULL;
}

static int prop_true(const owf_xml_node *props, const char *ns, const char *name)
{
    const char *v = prop(props, ns, name);
    return v && !strcmp(v, "true");
}

/* An ODF cell range address ("$Sales.$B$2:.$B$7") as Excel's reference, or NULL. */
static char *excel_ref(ods *o, const char *odf)
{
    owf_buf b;
    char *x;
    if (!odf || !*odf)
        return NULL;
    owf_buf_init(&b);
    owf_buf_puts(&b, "of:=[");
    owf_buf_puts(&b, odf);
    owf_buf_puts(&b, "]");
    owf_buf_putc(&b, 0);
    x = b.failed ? NULL : owf_of_to_excel((const char *)b.data);
    owf_buf_free(&b);
    (void)o;
    return x;
}

/* The cells of an ODF range on the book's sheets, one at a time (a row or a column of them): a cell, or NULL at
 * the end. *i counts; the sheet it is on when the range names none is home. */
static const owf_cell *range_cell(ods *o, const char *odf, int home, int i, int *done)
{
    char name[256] = "", second[256] = "";
    unsigned long r0, c0, r1, c1;
    const char *p = odf;
    int s = home;
    *done = 1;
    if (!p || !odf_cell(&p, name, sizeof name, &r0, &c0))
        return NULL;
    r1 = r0;
    c1 = c0;
    if (*p == ':' && (p++, !odf_cell(&p, second, sizeof second, &r1, &c1)))
        return NULL;
    if (*name) {
        for (s = 0; s < o->book->nsheets && strcmp(o->book->sheets[s].name, name); s++)
            ;
        if (s == o->book->nsheets)
            return NULL;
    }
    if (s < 0 || r1 < r0 || c1 < c0)
        return NULL;
    if (r0 == r1) {
        if ((unsigned long)i > c1 - c0)
            return NULL;
        *done = 0;
        return owf_sheet_find(&o->book->sheets[s], (unsigned)r0, (unsigned)(c0 + (unsigned long)i));
    }
    if ((unsigned long)i > r1 - r0)
        return NULL;
    *done = 0;
    return owf_sheet_find(&o->book->sheets[s], (unsigned)(r0 + (unsigned long)i), (unsigned)c0);
}

/* A series' points as its cells hold them now (no local table said them): values, and categories (x values). */
static int series_points(ods *o, owf_chart_series *se, const char *values, const char *cats, int scatter, int home)
{
    int n = 0, i, done;
    owf_chart_point *pts;
    for (i = 0; i < 4096; i++) {
        range_cell(o, values, home, i, &done);
        if (done)
            break;
        n = i + 1;
    }
    if (!n)
        return 1;
    if (!(pts = calloc((size_t)n, sizeof *pts)))
        return 0;
    se->points = pts;
    se->npoints = n;
    for (i = 0; i < n; i++) {
        const owf_cell *c = range_cell(o, values, home, i, &done);
        if (c && (c->type == OWF_VALUE_NUMBER || c->type == OWF_VALUE_BOOL)) {
            pts[i].value = c->number;
            pts[i].has_value = 1;
        }
        c = cats ? range_cell(o, cats, home, i, &done) : NULL;
        if (c && scatter && c->type == OWF_VALUE_NUMBER) {
            pts[i].x = c->number;
            pts[i].has_x = 1;
        } else if (c && c->type == OWF_VALUE_TEXT && c->text) {
            if (!(pts[i].label = copy(o, c->text)))
                return 0;
        } else if (c && c->type == OWF_VALUE_NUMBER) {
            char num[40];
            owf_number_text(c->number, num);
            if (!(pts[i].label = copy(o, num)))
                return 0;
        }
    }
    return 1;
}

/* The chart document's local table: the values the chart last had (a column of categories, one a series). */
static int table_points(ods *o, owf_chart *c, const owf_xml_node *table, int scatter)
{
    const owf_xml_node *rows = owf_xml_child(table, NS_TABLE, "table-rows"), *row, *head, *cell;
    int n = 0, k, i;
    for (row = owf_xml_child(rows, NS_TABLE, "table-row"); row; row = owf_xml_next(row, NS_TABLE, "table-row"))
        n++;
    head = owf_xml_child(owf_xml_child(table, NS_TABLE, "table-header-rows"), NS_TABLE, "table-row");
    for (i = 0, cell = owf_xml_child(head, NS_TABLE, "table-cell"); cell; cell = owf_xml_next(cell, NS_TABLE, "table-cell"), i++) {
        owf_buf t;
        if (!i || i > c->nseries || c->series[i - 1].name)
            continue;
        owf_buf_init(&t);
        cell_text_of(cell, &t);
        if (t.length) {
            owf_buf_putc(&t, 0);
            c->series[i - 1].name = t.failed ? NULL : copy(o, (const char *)t.data);
        }
        owf_buf_free(&t);
    }
    if (!n || n > 4096)
        return 1;
    for (k = 0; k < c->nseries; k++)
        if (!c->series[k].points) {
            if (!(c->series[k].points = calloc((size_t)n, sizeof *c->series[k].points)))
                return 0;
            c->series[k].npoints = n;
        } else {
            c->series[k].npoints = -c->series[k].npoints - 1;    /* points of its own: the table says none of them */
        }
    for (k = 0, row = owf_xml_child(rows, NS_TABLE, "table-row"); row; row = owf_xml_next(row, NS_TABLE, "table-row"), k++)
        for (i = 0, cell = owf_xml_child(row, NS_TABLE, "table-cell"); cell; cell = owf_xml_next(cell, NS_TABLE, "table-cell"), i++) {
            const char *type = owf_xml_attr(cell, NS_OFFICE, "value-type"), *v = owf_xml_attr(cell, NS_OFFICE, "value");
            double d;
            int s;
            if (!type)
                continue;
            if (!i) {                            /* the category (or x) of every series' point k that has them */
                for (s = 0; s < c->nseries; s++) {
                    owf_chart_point *p;
                    if (k >= c->series[s].npoints || !(scatter ? c->series[s].x_values != NULL : c->series[s].categories != NULL))
                        continue;
                    p = &c->series[s].points[k];
                    if (p->label || p->has_x)
                        continue;
                    if (!strcmp(type, "float") && v && scatter && owf_number_parse(v, strlen(v), &d)) {
                        p->x = d;
                        p->has_x = 1;
                    } else {
                        owf_buf t;
                        owf_buf_init(&t);
                        cell_text_of(cell, &t);
                        if (!t.length && v)
                            owf_buf_puts(&t, v);
                        owf_buf_putc(&t, 0);
                        if (t.length > 1 && !t.failed)
                            p->label = copy(o, (const char *)t.data);
                        owf_buf_free(&t);
                    }
                }
            } else if (i <= c->nseries && k < c->series[i - 1].npoints && v && owf_number_parse(v, strlen(v), &d) &&
                       !c->series[i - 1].points[k].has_value) {
                c->series[i - 1].points[k].value = d;
                c->series[i - 1].points[k].has_value = 1;
            }
        }
    for (k = 0; k < c->nseries; k++) {
        owf_chart_series *se = &c->series[k];
        if (se->npoints < 0) {
            se->npoints = -se->npoints - 1;
            continue;
        }
        while (se->npoints && !se->points[se->npoints - 1].has_value && !se->points[se->npoints - 1].label &&
               !se->points[se->npoints - 1].has_x && !se->points[se->npoints - 1].colour)
            se->npoints--;                       /* the table's rows past its last point */
    }
    return 1;
}

/* A tree's element as the stream's start event. */
static void node_event(const owf_xml_node *n, int depth, owf_xml_event *e)
{
    memset(e, 0, sizeof *e);
    e->type = OWF_XS_START;
    e->ns = n->ns;
    e->name = n->name;
    e->attrs = n->attrs;
    e->nattrs = n->nattrs;
    e->depth = depth;
}

/* A chart document's number style (in its automatic styles) read as the content's are. */
static void node_numstyle(ods *o, const owf_xml_node *st)
{
    const owf_xml_node *ch, *t;
    owf_xml_event e;
    node_event(st, 1, &e);
    start_numstyle(o, &e);
    for (ch = st->first; ch && o->num; ch = ch->next) {
        if (ch->type != OWF_XML_ELEMENT)
            continue;
        node_event(ch, 2, &e);
        numstyle_child(o, &e);
        if (o->num_text) {
            for (t = ch->first; t; t = t->next)
                if (t->type == OWF_XML_TEXT)
                    owf_buf_put(&o->num->code, t->name, t->length);
            num_text_end(o);
        }
    }
    o->num = NULL;
    o->num_text = 0;
}

/* The number format of a chart document's style (an axis's, a series' labels'), when it has its own, not
 * its values': *out set (or left NULL). 0 when memory ran out. */
static int chart_numfmt(ods *o, const owf_xml_node *styles, const char *name, char **out)
{
    const owf_xml_node *st, *props = NULL;
    const char *data = NULL, *link;
    const ods_numstyle *n;
    int keep = o->nnums, ok = 1;
    for (st = owf_xml_child(styles, NS_STYLE, "style"); name && st; st = owf_xml_next(st, NS_STYLE, "style")) {
        const char *v = owf_xml_attr(st, NS_STYLE, "name");
        if (v && !strcmp(v, name)) {
            data = owf_xml_attr(st, NS_STYLE, "data-style-name");
            props = owf_xml_child(st, NS_STYLE, "chart-properties");
            break;
        }
    }
    link = prop(props, NS_CHART, "link-data-style-to-source");
    if (!data || !link || strcmp(link, "false"))
        return 1;
    for (st = styles->first; st; st = st->next)
        if (st->type == OWF_XML_ELEMENT && !strcmp(st->ns, NS_NUMBER) && strstr(st->name, "-style"))
            node_numstyle(o, st);
    if ((n = find_num(o, data)) != NULL) {
        owf_buf b;
        owf_buf_init(&b);
        if (!numfmt_text(o, n, &b))
            ok = 0;
        else if (strcmp((const char *)b.data, "General") && !(*out = copy(o, (const char *)b.data)))
            ok = 0;
        owf_buf_free(&b);
    }
    while (o->nnums > keep)                  /* the chart's styles are its own: gone again */
        free_num(&o->nums[--o->nnums]);
    return ok;
}

/* An ODF chart (its document's root) as the model's: kind, grouping, title, legend, axes, series. 0 when memory
 * runs out. */
static int read_odf_chart(ods *o, const owf_xml_node *root, owf_chart *c, int home)
{
    static const char *const symbols[] = { "", "", "circle", "square", "diamond", "arrow-up", "x", "star", "plus",
                                           "horizontal-bar", "" };
    static const char *const places[] = { "", "center", "inside", "near-origin", "outside", "top", "bottom", "left", "right",
                                          "avoid-overlap" };
    const owf_xml_node *styles = owf_xml_child(root, NS_OFFICE, "automatic-styles");
    const owf_xml_node *ch = owf_xml_child(owf_xml_child(owf_xml_child(root, NS_OFFICE, "body"), NS_OFFICE, "chart"), NS_CHART, "chart");
    const owf_xml_node *plot = owf_xml_child(ch, NS_CHART, "plot-area"), *n, *ax, *plotp, *ser, *title, *legend;
    const char *cls = ch ? owf_xml_attr(ch, NS_CHART, "class") : NULL, *v, *cats = NULL, *last_x = NULL;
    const char *ext = o->ext_ns;
    int count = 0, i, scatter;
    if (!ch)
        return 1;
    plotp = chart_props(styles, owf_xml_attr(plot, NS_CHART, "style-name"), "chart-properties");
    c->kind = !cls ? OWF_CHART_OTHER : !strcmp(cls, "chart:bar") ? (prop_true(plotp, NS_CHART, "vertical") ? OWF_CHART_BAR : OWF_CHART_COLUMN) :
              !strcmp(cls, "chart:line") ? OWF_CHART_LINE : !strcmp(cls, "chart:area") ? OWF_CHART_AREA :
              !strcmp(cls, "chart:circle") || !strcmp(cls, "chart:ring") ? OWF_CHART_PIE :
              !strcmp(cls, "chart:scatter") ? OWF_CHART_SCATTER : OWF_CHART_OTHER;
    if ((v = owf_xml_attr(ch, ext, "other")) != NULL && !strcmp(v, "true"))
        c->kind = OWF_CHART_OTHER;
    scatter = c->kind == OWF_CHART_SCATTER;
    c->grouping = prop_true(plotp, NS_CHART, "percentage") ? OWF_CHART_PERCENT :
                  prop_true(plotp, NS_CHART, "stacked") ? OWF_CHART_STACKED : OWF_CHART_CLUSTERED;
    c->gap = (int)whole(owf_xml_attr(ch, ext, "gap"), whole(prop(plotp, NS_CHART, "gap-width"), 150, 0, 500), 0, 500);
    if (c->kind == OWF_CHART_PIE) {
        long a = whole(prop(plotp, NS_CHART, "angle-offset"), 90, -3600, 3600);
        c->first_angle = (int)(((90 - a) % 360 + 360) % 360);
        if (!strcmp(cls, "chart:ring"))
            c->hole = (int)whole(owf_xml_attr(ch, ext, "hole"), 50, 10, 90);
    }
    v = owf_xml_attr(ch, ext, "vary");
    c->vary = v ? !strcmp(v, "true") : c->kind == OWF_CHART_PIE;
    if ((title = owf_xml_child(ch, NS_CHART, "title")) != NULL) {
        owf_buf t;
        owf_buf_init(&t);
        all_text_of(title, &t);
        owf_buf_putc(&t, 0);
        if (!t.failed)
            c->title = copy(o, (const char *)t.data);
        owf_buf_free(&t);
        if (!c->title)
            return 0;
    }
    if ((legend = owf_xml_child(ch, NS_CHART, "legend")) != NULL) {
        v = owf_xml_attr(legend, NS_CHART, "legend-position");
        c->legend = !v ? 'r' : !strcmp(v, "start") ? 'l' : !strcmp(v, "top") ? 't' : !strcmp(v, "bottom") ? 'b' : 'r';
    }
    for (ax = owf_xml_child(plot, NS_CHART, "axis"); ax; ax = owf_xml_next(ax, NS_CHART, "axis")) {
        const char *dim = owf_xml_attr(ax, NS_CHART, "dimension");
        const owf_xml_node *ap = chart_props(styles, owf_xml_attr(ax, NS_CHART, "style-name"), "chart-properties");
        int grid = owf_xml_child(ax, NS_CHART, "grid") != NULL;
        double d;
        if (!dim)
            continue;
        if (!strcmp(dim, "x")) {
            const owf_xml_node *cat = owf_xml_child(ax, NS_CHART, "categories");
            if (cat)
                cats = owf_xml_attr(cat, NS_TABLE, "cell-range-address");
            if (scatter) {
                if (!c->x_format && !chart_numfmt(o, styles, owf_xml_attr(ax, NS_CHART, "style-name"), &c->x_format))
                    return 0;
                c->x_gridlines = grid || ((v = owf_xml_attr(ch, ext, "x-gridlines")) != NULL && !strcmp(v, "true"));
                if ((v = prop(ap, NS_CHART, "minimum")) != NULL && owf_number_parse(v, strlen(v), &d)) {
                    c->xmin = d;
                    c->has_xmin = 1;
                }
                if ((v = prop(ap, NS_CHART, "maximum")) != NULL && owf_number_parse(v, strlen(v), &d)) {
                    c->xmax = d;
                    c->has_xmax = 1;
                }
            }
        } else if (!strcmp(dim, "y") && !c->gridlines && !c->has_min && !c->has_max && !c->value_format) {
            c->gridlines = grid;
            if (!owf_xml_attr(ch, ext, "gap") && (v = prop(ap, NS_CHART, "gap-width")) != NULL)
                c->gap = (int)whole(v, 150, 0, 500);   /* LibreOffice's place for it */
            if (!chart_numfmt(o, styles, owf_xml_attr(ax, NS_CHART, "style-name"), &c->value_format))
                return 0;
            if ((v = prop(ap, NS_CHART, "minimum")) != NULL && owf_number_parse(v, strlen(v), &d)) {
                c->min = d;
                c->has_min = 1;
            }
            if ((v = prop(ap, NS_CHART, "maximum")) != NULL && owf_number_parse(v, strlen(v), &d)) {
                c->max = d;
                c->has_max = 1;
            }
        }
    }
    for (ser = owf_xml_child(plot, NS_CHART, "series"); ser && count < 64; ser = owf_xml_next(ser, NS_CHART, "series"))
        count++;
    if (count && !(c->series = calloc((size_t)count, sizeof *c->series)))
        return 0;
    for (ser = owf_xml_child(plot, NS_CHART, "series"); ser && c->nseries < count; ser = owf_xml_next(ser, NS_CHART, "series")) {
        owf_chart_series *se = &c->series[c->nseries];
        const char *style = owf_xml_attr(ser, NS_CHART, "style-name");
        const owf_xml_node *sp = chart_props(styles, style, "chart-properties"), *gp = chart_props(styles, style, "graphic-properties");
        const char *values = owf_xml_attr(ser, NS_CHART, "values-cell-range-address");
        const char *label = owf_xml_attr(ser, NS_CHART, "label-cell-address");
        const owf_xml_node *dom = owf_xml_child(ser, NS_CHART, "domain");
        const char *x = dom ? owf_xml_attr(dom, NS_TABLE, "cell-range-address") : last_x, *mycats = cats;
        unsigned long rgb;
        int lines = c->kind == OWF_CHART_LINE || scatter, k;
        se->index = (int)whole(owf_xml_attr(ser, ext, "index"), c->nseries, 0, 0x7FFF);
        se->line = !(gp && (v = prop(gp, NS_DRAW, "stroke")) != NULL && !strcmp(v, "none"));
        if (gp && owf_parse_colour(prop(gp, lines ? NS_SVG : NS_DRAW, lines ? "stroke-color" : "fill-color"), &rgb))
            se->colour = OWF_SHADE(rgb);
        else if (gp && owf_parse_colour(prop(gp, NS_DRAW, "fill-color"), &rgb))
            se->colour = OWF_SHADE(rgb);
        /* markers: none, automatic, or one by its name */
        if (lines) {
            v = prop(sp, NS_CHART, "symbol-type");
            se->marker = v && !strcmp(v, "none") ? OWF_MARKER_NONE : OWF_MARKER_AUTO;
            if (v && !strcmp(v, "named-symbol") && (v = prop(sp, NS_CHART, "symbol-name")) != NULL)
                for (k = 2; k < 10; k++)
                    if (!strcmp(v, symbols[k]))
                        se->marker = k;
            if ((v = owf_xml_attr(ser, ext, "marker")) != NULL)
                se->marker = !strcmp(v, "dot") ? OWF_MARKER_DOT : !strcmp(v, "dash") ? OWF_MARKER_DASH :
                             !strcmp(v, "triangle") ? OWF_MARKER_TRIANGLE : se->marker;
            se->marker_size = (int)whole(owf_xml_attr(ser, ext, "marker-size"), 0, 0, 72);
            if (!se->marker_size && (v = prop(sp, NS_CHART, "symbol-width")) != NULL) {
                int twips;
                if (owf_parse_length(v, &twips) && twips > 0)
                    se->marker_size = (twips + 10) / 20;
            }
        }
        /* labels: what they show, where */
        if ((v = prop(sp, NS_CHART, "data-label-number")) != NULL)
            se->labels |= !strcmp(v, "value") ? OWF_LABEL_VALUE : !strcmp(v, "percentage") ? OWF_LABEL_PERCENT :
                          !strcmp(v, "value-and-percentage") ? OWF_LABEL_VALUE | OWF_LABEL_PERCENT : 0;
        if (prop_true(sp, NS_CHART, "data-label-text"))
            se->labels |= OWF_LABEL_CATEGORY;
        if (prop_true(sp, NS_CHART, "data-label-series"))
            se->labels |= OWF_LABEL_SERIES;
        if (se->labels && (v = prop(sp, NS_CHART, "label-position")) != NULL)
            for (k = 1; k <= OWF_LABEL_BEST_FIT; k++)
                if (!strcmp(v, places[k]))
                    se->label_pos = k;
        if (!chart_numfmt(o, styles, style, &se->label_format))
            return 0;
        if ((v = owf_xml_attr(ser, ext, "format")) != NULL && !(se->format = copy(o, v)))
            return 0;
        if ((v = owf_xml_attr(ser, ext, "name")) != NULL && !(se->name = copy(o, v)))
            return 0;
        if ((v = owf_xml_attr(ser, ext, "categories")) != NULL) {     /* its own (empty: none) */
            if (*v && !(se->categories = copy(o, v)))
                return 0;
            mycats = NULL;
        }
        /* its cells */
        if (values && !(se->values = excel_ref(o, values)))
            owf_report_add(o->report, OWF_NOTE_LOST, "Some charts' series could not be read: their cells are not ones a chart shows");
        if (label && !(se->name_cell = excel_ref(o, label)))
            return 0;
        if (scatter && x && !(se->x_values = excel_ref(o, x)))
            return 0;
        last_x = x;                              /* a scatter's next series without its own: these */
        if (!scatter && mycats && !(se->categories = excel_ref(o, mycats)))
            return 0;
        if (label && !se->name) {                /* its name, as its cell has it */
            int done;
            const owf_cell *cell = range_cell(o, label, home, 0, &done);
            if (cell && cell->type == OWF_VALUE_TEXT && cell->text && !(se->name = copy(o, cell->text)))
                return 0;
        }
        /* a point of its own colour */
        for (k = 0, n = owf_xml_child(ser, NS_CHART, "data-point"); n; n = owf_xml_next(n, NS_CHART, "data-point")) {
            long rep = whole(owf_xml_attr(n, NS_CHART, "repeated"), 1, 1, 4096);
            const owf_xml_node *pp = chart_props(styles, owf_xml_attr(n, NS_CHART, "style-name"), "graphic-properties");
            if (pp && owf_parse_colour(prop(pp, NS_DRAW, "fill-color"), &rgb) && k < 4096) {
                if (k >= se->npoints) {
                    owf_chart_point *p = realloc(se->points, (size_t)(k + 1) * sizeof *p);
                    if (!p)
                        return 0;
                    memset(p + se->npoints, 0, (size_t)(k + 1 - se->npoints) * sizeof *p);
                    se->points = p;
                    se->npoints = k + 1;
                }
                se->points[k].colour = OWF_SHADE(rgb);
            }
            k += (int)rep;
        }
        c->nseries++;
    }
    /* the values: as their cells hold them, else (no cells, the chart's own data) as its local table has them */
    last_x = NULL;
    for (i = 0, ser = owf_xml_child(plot, NS_CHART, "series"); ser && i < c->nseries; ser = owf_xml_next(ser, NS_CHART, "series"), i++) {
        owf_chart_series *se = &c->series[i];
        const owf_xml_node *dom = owf_xml_child(ser, NS_CHART, "domain");
        const char *x = dom ? owf_xml_attr(dom, NS_TABLE, "cell-range-address") : last_x;
        const char *mycats = owf_xml_attr(ser, ext, "categories") ? NULL : cats;
        owf_chart_point *keep = se->points;
        int nkeep = se->npoints, k;
        last_x = x;
        se->points = NULL;
        se->npoints = 0;
        if (!series_points(o, se, owf_xml_attr(ser, NS_CHART, "values-cell-range-address"), scatter ? x : mycats, scatter, home))
            return 0;
        if (!se->npoints) {                          /* none: its own colours stay for the table's */
            se->points = keep;
            se->npoints = nkeep;
            continue;
        }
        for (k = 0; k < nkeep; k++) {
            if (k < se->npoints)
                se->points[k].colour = keep[k].colour;
            free(keep[k].label);
        }
        free(keep);
    }
    if ((n = owf_xml_child(ch, NS_TABLE, "table")) != NULL && !table_points(o, c, n, scatter))
        return 0;
    return 1;
}

/* After the sheets: each frame's chart read, put on its sheet as an .xlsx's drawing would keep it. */
static void read_charts(ods *o, const owf_unzip *zip)
{
    int s, i, k;
    for (i = 1; i < o->nframes; i++)                 /* by their z-index: the order they were in */
        for (k = i; k > 0 && o->frames[k - 1].z > o->frames[k].z; k--) {
            ods_frame t = o->frames[k];
            o->frames[k] = o->frames[k - 1];
            o->frames[k - 1] = t;
        }
    for (s = 0; s < o->book->nsheets && !o->failed; s++) {
        owf_chart *charts = NULL;
        int n = 0, cap = 0;
        for (i = 0; i < o->nframes && !o->failed; i++) {
            ods_frame *f = &o->frames[i];
            owf_xml *xml = NULL;
            unsigned char *data = NULL;
            size_t length = 0;
            int e = OWF_OK;
            owf_chart *c;
            if (f->sheet != s)
                continue;
            if (f->xml) {
                xml = owf_xml_parse(f->xml, strlen(f->xml), &e);
            } else if (f->object && zip) {
                char name[600];
                snprintf(name, sizeof name, "%s/content.xml", f->object);
                if (owf_unzip_read(zip, name, &data, &length) == OWF_OK)
                    xml = owf_xml_parse((const char *)data, length, &e);
            }
            free(data);
            if (!xml) {
                if (e == OWF_ERR_MEMORY)
                    o->failed = 1;
                else if (f->object || f->xml)
                    owf_report_add(o->report, OWF_NOTE_LOST, "Some charts could not be read: their part of the file is missing or damaged");
                continue;
            }
            if (n == cap) {
                owf_chart *p = realloc(charts, (size_t)(cap ? cap * 2 : 4) * sizeof *p);
                if (!p) {
                    o->failed = 1;
                    owf_xml_free(xml);
                    break;
                }
                charts = p;
                cap = cap ? cap * 2 : 4;
            }
            c = &charts[n++];
            memset(c, 0, sizeof *c);
            /* where: its cell and the offsets in it, to another or with a size */
            if (f->absolute) {
                const owf_sheet *sh = &o->book->sheets[s];
                long rest;
                unsigned k;
                for (k = 0, rest = f->x; k < MAX_COLS - 1 && k < 100000U; k++) {
                    long w = sheet_line(sh, 1, k) * 635L;
                    if (rest < w)
                        break;
                    rest -= w;
                }
                c->from_col = k;
                c->from_col_off = rest;
                for (k = 0, rest = f->y; k < MAX_ROWS - 1 && k < 1000000U; k++) {
                    long h = sheet_line(sh, 0, k) * 635L;
                    if (rest < h)
                        break;
                    rest -= h;
                }
                c->from_row = k;
                c->from_row_off = rest;
            } else {
                c->from_col = f->col;
                c->from_row = f->row;
                c->from_col_off = f->x;
                c->from_row_off = f->y;
            }
            if (f->has_end && !f->absolute) {
                c->to_col = f->end_col;
                c->to_row = f->end_row;
                c->to_col_off = f->end_x;
                c->to_row_off = f->end_y;
            } else {
                c->sized = 1;
                c->width = f->width;
                c->height = f->height;
            }
            if (!read_odf_chart(o, owf_xml_root(xml), c, s))
                o->failed = 1;
            owf_xml_free(xml);
        }
        if (n && !o->failed && owf_sheet_keep_charts(o->book, s, charts, n) != OWF_OK)
            o->failed = 1;
        owf_charts_free(charts, n);
    }
}

/* ---- The stream ---- */

static void text_start(ods *o, const owf_xml_event *e)
{
    if (o->inline_depth) {                       /* a flat file's chart, inside its frame */
        inline_start(o, e);
        return;
    }
    if (o->frame_depth) {
        frame_child(o, e);
        return;
    }
    if (o->skip_depth)
        return;
    if (o->note_depth) {                         /* the cell's comment: its author and paragraphs */
        if (is(e, NS_TEXT, "p") || is(e, NS_TEXT, "h")) {
            if (o->note_paras++)
                owf_buf_putc(&o->note_text, '\n');
            o->note_p = 1;
        } else if (is(e, NS_DC, "creator")) {
            o->note_who = 1;
        } else if (is(e, NS_TEXT, "s") && o->note_p) {
            long c = whole(ev(e, NS_TEXT, "c"), 1, 1, 1000);
            while (c-- > 0)
                owf_buf_putc(&o->note_text, ' ');
        } else if (is(e, NS_TEXT, "tab") && o->note_p) {
            owf_buf_putc(&o->note_text, '\t');
        } else if (is(e, NS_TEXT, "line-break") && o->note_p) {
            owf_buf_putc(&o->note_text, '\n');
        }
        return;
    }
    if (is(e, NS_OFFICE, "annotation")) {
        o->note_depth = e->depth;
        o->note_text.length = o->note_author.length = 0;
        o->note_text.failed = o->note_author.failed = 0;
        o->note_paras = o->note_p = o->note_who = 0;
    } else if (is(e, NS_DRAW, "frame")) {
        frame_start(o, e, 1);
    } else if (!strcmp(e->ns, NS_DRAW)) {
        owf_report_add(o->report, OWF_NOTE_LOST, "Drawings (shapes, lines) on sheets are not read yet");
        o->skip_depth = e->depth;
    } else if (is(e, NS_TEXT, "p") || is(e, NS_TEXT, "h")) {
        if (o->paras++)
            owf_buf_putc(&o->text, '\n');
        o->in_p = 1;
    } else if (is(e, NS_TEXT, "s")) {
        long c = whole(ev(e, NS_TEXT, "c"), 1, 1, 10000);
        if (o->text.length + (unsigned long)c > MAX_CELL_TEXT)
            owf_report_add(o->report, OWF_NOTE_LOST, "Text longer than 131072 bytes in a cell was cut short");
        while (c-- > 0 && o->text.length < MAX_CELL_TEXT)
            owf_buf_putc(&o->text, ' ');
    } else if (is(e, NS_TEXT, "tab")) {
        owf_buf_putc(&o->text, '\t');
    } else if (is(e, NS_TEXT, "line-break")) {
        owf_buf_putc(&o->text, '\n');
    } else if (is(e, NS_TEXT, "a")) {
        owf_report_add(o->report, OWF_NOTE_LOST, "Links in cells keep their text; where they go is not read yet");
    } else if (is(e, NS_TEXT, "note")) {
        o->skip_depth = e->depth;
    }
}

static void body_start(ods *o, const owf_xml_event *e)
{
    if (o->in_cell) {
        text_start(o, e);
        return;
    }
    if (o->inline_depth) {                       /* a chart's frame in table:shapes */
        inline_start(o, e);
        return;
    }
    if (o->frame_depth) {
        frame_child(o, e);
        return;
    }
    if (o->rule_cur && !o->sheet) {              /* inside a rule: its messages */
        if (is(e, NS_TABLE, "help-message"))
            rule_message(o, e, 1);
        else if (is(e, NS_TABLE, "error-message"))
            rule_message(o, e, 2);
        else if (o->rule_msg && (is(e, NS_TEXT, "p") || is(e, NS_TEXT, "h"))) {
            if (o->rule_paras++)
                owf_buf_putc(&o->rule_text, '\n');
            o->rule_p = 1;
        }
        return;
    }
    if (o->af_sheet && !o->sheet) {              /* inside an AutoFilter: its conditions */
        filter_child(o, e);
        return;
    }
    if (!strcmp(e->ns, NS_TABLE)) {
        const char *n = e->name;
        if (!strcmp(n, "table") && !o->sheet && !o->skip_depth) {
            start_table(o, e);
        } else if (!o->sheet) {
            if (!strcmp(n, "null-date")) {
                const char *v = ev(e, NS_TABLE, "date-value");
                o->null_date_1904 = v && !strncmp(v, "1904-01-01", 10);
                o->null_date_other = v && !o->null_date_1904 && strncmp(v, "1899-12-30", 10);
            } else if (!strcmp(n, "named-range")) {
                named(o, e, 0);
            } else if (!strcmp(n, "named-expression")) {
                named(o, e, 1);
            } else if (!strcmp(n, "content-validation")) {
                rule_start(o, e);
            } else if (!strcmp(n, "database-range")) {
                filter_start(o, e);
            } else if (!strcmp(n, "dde-link")) {
                owf_report_add(o->report, OWF_NOTE_LOST, "Links to other programs (DDE) keep their last values");
            }
        } else if (!strcmp(n, "table-column")) {
            table_column(o, e);
        } else if (!strcmp(n, "table-row")) {
            start_row(o, e);
        } else if (!strcmp(n, "table-cell") || !strcmp(n, "covered-table-cell")) {
            start_cell(o, e);
            if (e->empty)
                o->text.length = 0;
        } else if (!strcmp(n, "named-range")) {
            named(o, e, 0);
        } else if (!strcmp(n, "named-expression")) {
            named(o, e, 1);
        } else if (!strcmp(n, "shapes")) {
            o->in_shapes = 1;                    /* its frames: from the sheet's corner */
        } else if (!strcmp(n, "table") && o->sheet) {
            o->skip_depth = e->depth;    /* a table inside a cell's text: not the model's */
        }
    } else if (o->sheet && !strcmp(e->ns, NS_CALCEXT)) {
        cf_start(o, e);
    } else if (is(e, NS_DRAW, "frame") && o->sheet) {
        frame_start(o, e, 0);
    } else if (!strcmp(e->ns, NS_DRAW) && o->sheet) {
        owf_report_add(o->report, OWF_NOTE_LOST, "Drawings (shapes, lines) on sheets are not read yet");
        o->skip_depth = e->depth;
    }
}

static void body_end(ods *o, const owf_xml_event *e)
{
    if (o->skip_depth) {
        if (e->depth == o->skip_depth)
            o->skip_depth = 0;
        return;
    }
    if (o->inline_depth) {
        inline_end(o, e);
        return;
    }
    if (o->frame_depth) {
        if (e->depth == o->frame_depth)
            o->frame_depth = 0;
        return;
    }
    if (o->in_cell) {
        if (o->note_depth) {
            if (e->depth == o->note_depth) {
                o->note_depth = 0;
                o->note_ready = 1;
            } else if (is(e, NS_TEXT, "p") || is(e, NS_TEXT, "h")) {
                o->note_p = 0;
            } else if (is(e, NS_DC, "creator")) {
                o->note_who = 0;
            }
        } else if (is(e, NS_TABLE, "table-cell") || is(e, NS_TABLE, "covered-table-cell"))
            end_cell(o);
        else if (is(e, NS_TEXT, "p") || is(e, NS_TEXT, "h"))
            o->in_p = 0;
        return;
    }
    if (!o->sheet) {
        if (o->rule_cur) {
            if (is(e, NS_TABLE, "content-validation"))
                o->rule_cur = 0;
            else if (is(e, NS_TABLE, "help-message") || is(e, NS_TABLE, "error-message"))
                rule_message_end(o);
            else if (is(e, NS_TEXT, "p") || is(e, NS_TEXT, "h"))
                o->rule_p = 0;
        } else if (o->af_sheet) {
            if (is(e, NS_TABLE, "database-range"))
                filter_end(o);
            else if (is(e, NS_TABLE, "filter-condition"))
                filter_condition_end(o);
        }
        return;
    }
    if (is(e, NS_TABLE, "table-row"))
        end_row(o);
    else if (is(e, NS_TABLE, "table") && e->depth == o->table_depth)
        end_table(o);
    else if (!strcmp(e->ns, NS_CALCEXT))
        cf_end(o, e);
}

/* One part (content.xml, styles.xml, settings.xml) or the whole of a
 * flat file, as a stream. */
static int read_part(ods *o, char *data, size_t length)
{
    owf_xml_scan *scan = owf_xml_scan_new(data, length);
    owf_xml_event e;
    int r = 0, style_depth = 0, num_depth = 0, settings = 0, first = 1;
    if (!scan) {
        o->failed = 1;
        return OWF_ERR_MEMORY;
    }
    while (!o->failed && (r = owf_xml_scan_next(scan, &e)) > 0) {
        if (first && e.type == OWF_XS_START) {
            first = 0;
            if (strcmp(e.ns, NS_OFFICE)) {
                owf_xml_scan_free(scan);
                return OWF_ERR_FORMAT;
            }
        }
        if (e.type == OWF_XS_TEXT) {
            if (o->inline_depth) {
                owf_buf t;                       /* the text, escaped again */
                owf_buf_init(&t);
                owf_buf_put(&t, e.text, e.length);
                owf_buf_putc(&t, 0);
                if (!t.failed)
                    put_escaped(&o->inline_xml, (const char *)t.data, 0);
                owf_buf_free(&t);
            } else if (o->note_depth && !o->skip_depth) {
                if (o->note_p && o->note_text.length + e.length <= MAX_CELL_TEXT)
                    owf_buf_put(&o->note_text, e.text, e.length);
                else if (o->note_who && o->note_author.length + e.length <= 1024)
                    owf_buf_put(&o->note_author, e.text, e.length);
            } else if (o->rule_msg && o->rule_p) {
                if (o->rule_text.length + e.length <= 65536)
                    owf_buf_put(&o->rule_text, e.text, e.length);
            } else if (o->in_cell && o->in_p && !o->skip_depth) {
                if (o->text.length + e.length <= MAX_CELL_TEXT)
                    owf_buf_put(&o->text, e.text, e.length);
                else
                    owf_report_add(o->report, OWF_NOTE_LOST, "Text longer than 131072 bytes in a cell was cut short");
            }
            else if (o->num_text)
                owf_buf_put(&o->num->code, e.text, e.length);
            else if (o->cfg_item)
                owf_buf_put(&o->cfg_text, e.text, e.length);
            continue;
        }
        if (e.type == OWF_XS_START && o->inline_depth) {   /* a flat file's chart document: copied whole */
            inline_start(o, &e);
            continue;
        }
        if (e.type == OWF_XS_END && o->inline_depth) {
            inline_end(o, &e);
            continue;
        }
        if (e.type == OWF_XS_START) {
            if (num_depth && e.depth > num_depth) {
                if (o->num && !o->num_text)  /* number:text holds only text */
                    numstyle_child(o, &e);
                continue;
            }
            if (style_depth && e.depth > style_depth) {
                style_properties(o, &e);
                continue;
            }
            if (is(&e, NS_STYLE, "style") || is(&e, NS_STYLE, "default-style")) {
                start_style(o, &e, e.name[0] == 'd');
                style_depth = e.depth;
            } else if (!strcmp(e.ns, NS_NUMBER) && strstr(e.name, "-style")) {
                start_numstyle(o, &e);
                num_depth = e.depth;
            } else if (is(&e, NS_STYLE, "font-face")) {
                read_face(o, &e);
            } else if (is(&e, NS_OFFICE, "body") || is(&e, NS_OFFICE, "spreadsheet")) {
                if (!o->in_body) {       /* cells with no style have the default's */
                    owf_cellstyle base;
                    base_style(o, &base);
                    o->book->styles[0] = base;
                }
                o->in_body = 1;
            } else if (is(&e, NS_OFFICE, "settings")) {
                settings = 1;
            } else if (is(&e, NS_OFFICE, "text") || is(&e, NS_OFFICE, "presentation") || is(&e, NS_OFFICE, "drawing")) {
                owf_xml_scan_free(scan);
                return OWF_ERR_FORMAT;  /* a document of another kind */
            } else if (settings && !strcmp(e.ns, NS_CONFIG)) {
                config_start(o, &e);
            } else if (o->in_body) {
                body_start(o, &e);
            }
            continue;
        }
        /* an end */
        if (num_depth) {
            if (o->num && o->num_text && e.depth == o->num_text_depth)
                num_text_end(o);
            if (e.depth == num_depth) {
                num_depth = 0;
                o->num = NULL;
                o->num_text = 0;
            }
            continue;
        }
        if (style_depth && e.depth == style_depth) {
            style_depth = 0;
            o->style = NULL;
            continue;
        }
        if (style_depth)
            continue;
        if (settings && !strcmp(e.ns, NS_CONFIG)) {
            config_end(o, &e);
            continue;
        }
        if (is(&e, NS_OFFICE, "settings"))
            settings = 0;
        else if (o->in_body)
            body_end(o, &e);
    }
    if (r < 0) {
        if (owf_xml_scan_error(scan) == OWF_ERR_MEMORY)
            o->failed = 1;
        else
            o->damaged = 1;
    }
    owf_xml_scan_free(scan);
    return OWF_OK;
}

/* ---- The whole spreadsheet ---- */

static void finish(ods *o)
{
    int i, k;
    if (o->style_maps && !o->cf_seen)                /* ODF's own conditional styles alone: not read */
        owf_report_add(o->report, OWF_NOTE_LOST, "Conditional formatting is not read yet");
    for (i = 0; i < o->nnames && !o->failed; i++)
        if (owf_book_add_name(o->book, o->names[i].name, o->names[i].sheet, o->names[i].formula, 0) < 0)
            o->failed = 1;
    for (i = 0; i < o->book->nsheets; i++) {
        owf_sheet *sh = &o->book->sheets[i];
        if (o->active && !strcmp(sh->name, o->active))
            o->book->active = i;
        for (k = 0; k < o->nsplits; k++)
            if (!strcmp(o->splits[k].sheet, sh->name)) {
                const ods_split *sp = &o->splits[k];
                if (sp->hmode == 2 && sp->hpos > 0 && (unsigned long)sp->hpos < MAX_COLS)
                    sh->freeze_cols = (unsigned)sp->hpos;
                if (sp->vmode == 2 && sp->vpos > 0 && (unsigned long)sp->vpos < MAX_ROWS)
                    sh->freeze_rows = (unsigned)sp->vpos;
                if (sp->hmode == 1 || sp->vmode == 1)
                    owf_report_add(o->report, OWF_NOTE_LOST, "Windows split in two are shown whole");
            }
    }
    o->book->date1904 = o->null_date_1904;
    if (o->null_date_other)
        owf_report_add(o->report, OWF_NOTE_APPROX, "Dates are counted from 30 December 1899, not from the day this spreadsheet counts them from: formulas that make dates from numbers may differ");
    if (!o->book->nsheets)
        owf_report_add(o->report, OWF_NOTE_INFO, "The workbook has no worksheets");
    if (o->damaged)
        owf_report_add(o->report, OWF_NOTE_LOST, "The file is damaged or cut short; what could be read was read");
}

static void free_reader(ods *o)
{
    int i;
    for (i = 0; i < o->nstyles; i++) {
        free(o->styles[i].name);
        free(o->styles[i].parent);
        free(o->styles[i].data_style);
        free(o->styles[i].font);
    }
    free(o->styles);
    for (i = 0; i < o->nnums; i++) {
        free_num(&o->nums[i]);
    }
    free(o->nums);
    for (i = 0; i < o->nfaces; i++) {
        free(o->faces[i].name);
        free(o->faces[i].family);
    }
    free(o->faces);
    free(o->cols);
    for (i = 0; i < o->narrays; i++)
        free(o->arrays[i].text);
    free(o->arrays);
    owf_buf_free(&o->text);
    for (i = 0; i < o->nnames; i++) {
        free(o->names[i].name);
        free(o->names[i].formula);
    }
    free(o->names);
    free(o->active);
    for (i = 0; i < o->nsplits; i++)
        free(o->splits[i].sheet);
    free(o->splits);
    owf_buf_free(&o->cfg_text);
    for (i = 0; i < o->nrules; i++) {
        free(o->rules[i].name);
        owf_validation_free(&o->rules[i].v);
    }
    free(o->rules);
    for (i = 0; i < o->nuses; i++)
        free(o->uses[i].ranges);
    free(o->uses);
    free(o->runs);
    owf_buf_free(&o->rule_text);
    owf_buf_free(&o->note_text);
    owf_buf_free(&o->note_author);
    owf_buf_free(&o->af_xml);
    free(o->af_value);
    free(o->cf_ranges);
    owf_buf_free(&o->cf_xml);
    for (i = 0; i < o->nframes; i++) {
        free(o->frames[i].object);
        free(o->frames[i].xml);
    }
    free(o->frames);
    owf_buf_free(&o->inline_xml);
}

static ods *new_reader(owf_book *book, owf_report *report)
{
    ods *o = calloc(1, sizeof *o);
    if (!o)
        return NULL;
    o->book = book;
    o->report = report;
    o->ext_ns = NS_GFO;
    o->repeat_budget = REPEAT_BUDGET;
    o->empty_budget = EMPTY_BUDGET;
    o->row_budget = ROW_BUDGET;
    o->array_budget = ARRAY_BUDGET;
    o->text_budget = TEXT_BUDGET;
    owf_buf_init(&o->text);
    owf_buf_init(&o->cfg_text);
    owf_buf_init(&o->rule_text);
    owf_buf_init(&o->note_text);
    owf_buf_init(&o->note_author);
    owf_buf_init(&o->af_xml);
    owf_buf_init(&o->cf_xml);
    owf_buf_init(&o->inline_xml);
    return o;
}

static int done(ods *o, int result)
{
    if (result == OWF_OK && o->failed)
        result = OWF_ERR_MEMORY;
    if (result == OWF_OK)
        finish(o);
    if (result == OWF_OK && o->failed)
        result = OWF_ERR_MEMORY;
    free_reader(o);
    free(o);
    return result;
}

/* A part of the package, read as a stream; a missing optional part is no
 * error. */
static int package_part(ods *o, const owf_unzip *zip, const char *name, int needed)
{
    unsigned char *data;
    size_t length;
    int r = owf_unzip_read(zip, name, &data, &length);
    if (r != OWF_OK) {
        if (r == OWF_ERR_MEMORY) {
            o->failed = 1;
            return r;
        }
        if (needed)
            return r == OWF_ERR_FORMAT ? OWF_ERR_CORRUPT : r;
        if (owf_unzip_has(zip, name))
            o->damaged = 1;
        return OWF_OK;
    }
    r = read_part(o, (char *)data, length);
    free(data);
    return r;
}

static int import_ods(const unsigned char *data, size_t length, owf_book *book, owf_report *report)
{
    owf_unzip zip;
    ods *o;
    int result;
    if (!owf_unzip_open(&zip, data, length))
        return OWF_ERR_FORMAT;
    if (owf_odf_kind(data, length) != OWF_PACKAGE_SHEET && owf_odf_kind(data, length) != OWF_PACKAGE_UNKNOWN)
        return OWF_ERR_FORMAT;
    if (!(o = new_reader(book, report)))
        return OWF_ERR_MEMORY;
    result = package_part(o, &zip, "styles.xml", 0);
    if (result == OWF_OK)
        result = package_part(o, &zip, "content.xml", 1);
    if (result == OWF_OK)
        read_charts(o, &zip);
    if (result == OWF_OK)
        result = package_part(o, &zip, "settings.xml", 0);
    return done(o, result);
}

static int import_fods(const unsigned char *data, size_t length, owf_book *book, owf_report *report)
{
    ods *o;
    char *text;
    int result;
    if (!(o = new_reader(book, report)))
        return OWF_ERR_MEMORY;
    text = malloc(length + 1);
    if (!text) {
        free(o);
        return OWF_ERR_MEMORY;
    }
    memcpy(text, data, length);
    result = read_part(o, text, length);
    free(text);
    if (result == OWF_OK)
        read_charts(o, NULL);
    return done(o, result);
}

static int detect_ods(const unsigned char *data, size_t length)
{
    owf_unzip zip;
    int kind = owf_odf_kind(data, length);
    if (kind == OWF_PACKAGE_SHEET)
        return 100;
    if (kind == OWF_PACKAGE_UNKNOWN && owf_unzip_open(&zip, data, length) && owf_unzip_has(&zip, "content.xml") &&
        !owf_unzip_has(&zip, "xl/workbook.xml")) {
        unsigned char *content;
        size_t n;
        int score = 0;
        /* no mimetype: a spreadsheet's body says so */
        if (owf_unzip_read(&zip, "content.xml", &content, &n) == OWF_OK) {
            if (strstr((const char *)content, "office:spreadsheet") != NULL)
                score = 60;
            free(content);
        }
        return score;
    }
    return 0;
}

static int detect_fods(const unsigned char *data, size_t length)
{
    static const char mime[] = "application/vnd.oasis.opendocument.spreadsheet";
    size_t n = length < 4096 ? length : 4096, i, j;
    for (i = 0; i + 16 < n; i++)
        if (data[i] == '<' && !memcmp(data + i, "<office:document", 16) &&
            (data[i + 16] == ' ' || data[i + 16] == '\n' || data[i + 16] == '\r' || data[i + 16] == '\t')) {
            for (j = i; j + sizeof mime - 1 < n; j++)
                if (!memcmp(data + j, mime, sizeof mime - 1))
                    return 90;
            return 0;
        }
    return 0;
}

const owf_book_format owf_book_format_ods = {
    "ods", "OpenDocument spreadsheet", "ods", detect_ods, import_ods, owf_export_ods
};

const owf_book_format owf_book_format_fods = {
    "fods", "OpenDocument spreadsheet, flat XML", "fods", detect_fods, import_fods, owf_export_fods
};
