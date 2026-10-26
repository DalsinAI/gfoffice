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
 * active sheet (settings.xml), named ranges and expressions, and the 1904
 * date system (the null date).
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
} ods;

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
        owf_report_add(o->report, OWF_NOTE_LOST, "Conditional formatting is not read yet");
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
static int numfmt_index(ods *o, const char *name)
{
    const ods_numstyle *n = find_num(o, name), *pos = NULL, *neg = NULL, *zero = NULL, *posz = NULL, *any = NULL;
    owf_buf b;
    int i, index;
    if (!n)
        return 0;
    if (n->excel)
        return (index = owf_book_numfmt(o->book, n->excel)) < 0 ? (o->failed = 1, 0) : index;
    owf_buf_init(&b);
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
    if (b.failed) {
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
    char *excel = NULL;
    int style, k, has, explicit_style;
    owf_cell probe;

    o->in_cell = 0;
    if (o->col >= MAX_COLS || o->row >= MAX_ROWS) {
        o->col += (unsigned long)repeat;
        return;
    }
    if ((unsigned long)repeat > MAX_COLS - o->col)
        repeat = (long)(MAX_COLS - o->col);
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
    if (o->row >= MAX_ROWS)
        return;
    if (n > MAX_ROWS - o->row)
        n = MAX_ROWS - o->row;
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
    if (o->row_cell_style) {                       /* the row's own formatting, for its empty cells */
        int st = model_style(o, o->row_cell_style);
        for (k = 0; st > 0 && k < n && !o->failed; k++) {
            if (k && !o->row_budget--) {
                owf_report_add(o->report, OWF_NOTE_LOST, "Some repeated rows' formatting was left out: too many of them");
                break;
            }
            if (owf_sheet_set_row_style(o->sheet, (unsigned)(o->row + k), st) < 0)
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

/* ---- The stream ---- */

static void text_start(ods *o, const owf_xml_event *e)
{
    if (o->skip_depth)
        return;
    if (is(e, NS_OFFICE, "annotation")) {
        owf_report_add(o->report, OWF_NOTE_LOST, "Comments on cells are not read yet");
        o->skip_depth = e->depth;
    } else if (!strcmp(e->ns, NS_DRAW)) {
        owf_report_add(o->report, OWF_NOTE_LOST, "Pictures, charts and drawings on sheets are not read yet");
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
                owf_report_add(o->report, OWF_NOTE_LOST, "Data validation (lists to pick from, limits) is not read yet");
            } else if (!strcmp(n, "database-range") && ev(e, NS_TABLE, "display-filter-buttons")) {
                owf_report_add(o->report, OWF_NOTE_LOST, "AutoFilter buttons are not read yet");
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
            owf_report_add(o->report, OWF_NOTE_LOST, "Pictures, charts and drawings on sheets are not read yet");
        } else if (!strcmp(n, "table") && o->sheet) {
            o->skip_depth = e->depth;    /* a table inside a cell's text: not the model's */
        }
    } else if (is(e, NS_CALCEXT, "conditional-formats")) {
        owf_report_add(o->report, OWF_NOTE_LOST, "Conditional formatting is not read yet");
    } else if (!strcmp(e->ns, NS_DRAW) && o->sheet) {
        owf_report_add(o->report, OWF_NOTE_LOST, "Pictures, charts and drawings on sheets are not read yet");
    }
}

static void body_end(ods *o, const owf_xml_event *e)
{
    if (o->skip_depth) {
        if (e->depth == o->skip_depth)
            o->skip_depth = 0;
        return;
    }
    if (o->in_cell) {
        if (is(e, NS_TABLE, "table-cell") || is(e, NS_TABLE, "covered-table-cell"))
            end_cell(o);
        else if (is(e, NS_TEXT, "p") || is(e, NS_TEXT, "h"))
            o->in_p = 0;
        return;
    }
    if (!o->sheet)
        return;
    if (is(e, NS_TABLE, "table-row"))
        end_row(o);
    else if (is(e, NS_TABLE, "table") && e->depth == o->table_depth)
        end_table(o);
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
            if (o->in_cell && o->in_p && !o->skip_depth) {
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
            if (o->num && o->num_text && e.depth == o->num_text_depth) {
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
    int i, k;
    for (i = 0; i < o->nstyles; i++) {
        free(o->styles[i].name);
        free(o->styles[i].parent);
        free(o->styles[i].data_style);
        free(o->styles[i].font);
    }
    free(o->styles);
    for (i = 0; i < o->nnums; i++) {
        free(o->nums[i].name);
        free(o->nums[i].excel);
        owf_buf_free(&o->nums[i].code);
        for (k = 0; k < o->nums[i].nmaps; k++) {
            free(o->nums[i].maps[k].cond);
            free(o->nums[i].maps[k].apply);
        }
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
