/*
 * libowf: Excel workbooks in Office Open XML (.xlsx, .xltx, and .xlsm and
 * .xltm without their macros), read from ECMA-376 (transitional, and
 * strict's namespaces). The parts are found through the relationships, as
 * Excel finds them. Cells and their values (shared strings, rich text
 * flattened, inline strings, booleans, errors, dates), formulas as Excel
 * shows them (shared ones moved to each cell, array ones on each cell of
 * the range, other books by name), the cells' styles (fonts, number
 * formats, alignment, wrap, fills, borders, theme colours with their
 * tints), column widths, row heights, hidden rows, columns and sheets,
 * merged cells, frozen panes, defined names, the active sheet and the
 * 1904 date system, conditional formatting (cellIs, expression and colour
 * scale rules worked out; the others kept as written). What the model
 * cannot hold (comments, pictures, charts, validation, tables, pivot
 * tables, macros) is reported.
 *
 * Big parts (the sheets, the shared strings) are read as a stream, element
 * by element, so a sheet's memory is its cells', not a tree of its XML.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "owf_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NS_MAIN "http://schemas.openxmlformats.org/spreadsheetml/2006/main"
#define NS_MAIN_STRICT "http://purl.oclc.org/ooxml/spreadsheetml/main"
#define NS_REL "http://schemas.openxmlformats.org/officeDocument/2006/relationships"
#define NS_REL_STRICT "http://purl.oclc.org/ooxml/officeDocument/relationships"
#define NS_A "http://schemas.openxmlformats.org/drawingml/2006/main"
#define NS_A_STRICT "http://purl.oclc.org/ooxml/drawingml/main"

#define MAX_ROW 1048575UL
#define MAX_COL 16383UL
#define MAX_ARRAYS 4096                  /* array formulas a sheet spreads over their ranges */
#define MAX_ARRAY_CELLS 262144UL         /* cells the book's array formulas may add */
#define TEXT_BUDGET (64UL * 1024 * 1024) /* bytes of shared strings copied into cells: a short
                                            file must not ask for gigabytes */
#define PART_NAME 512

/* ---- Small helpers ---- */

static int is_main(const char *ns)
{
    return !strcmp(ns, NS_MAIN) || !strcmp(ns, NS_MAIN_STRICT);
}

static int main_is(const owf_xml_node *n, const char *name)
{
    return n && n->type == OWF_XML_ELEMENT && is_main(n->ns) && !strcmp(n->name, name);
}

static owf_xml_node *main_child(const owf_xml_node *n, const char *name)
{
    owf_xml_node *c;
    for (c = n ? n->first : NULL; c; c = c->next)
        if (main_is(c, name))
            return c;
    return NULL;
}

static owf_xml_node *main_next(const owf_xml_node *n, const char *name)
{
    owf_xml_node *c;
    for (c = n ? n->next : NULL; c; c = c->next)
        if (main_is(c, name))
            return c;
    return NULL;
}

static const char *attr(const owf_xml_node *n, const char *name)
{
    return owf_xml_attr(n, "", name);
}

static const char *rel_attr(const owf_xml_node *n, const char *name)
{
    const char *v = owf_xml_attr(n, NS_REL, name);
    return v ? v : owf_xml_attr(n, NS_REL_STRICT, name);
}

static const char *ev_attr(const owf_xml_event *e, const char *name)
{
    return owf_xml_event_attr(e, "", name);
}

/* ST_Boolean: "1" or "true"; def when absent. */
static int xbool(const char *v, int def)
{
    if (!v)
        return def;
    return !strcmp(v, "1") || !strcmp(v, "true") || !strcmp(v, "on");
}

static long xlong(const char *v, long def, long min, long max)
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

static int xdouble(const char *v, double *d)
{
    return v && owf_number_parse(v, strlen(v), d);
}

static long rounded(double v)
{
    return v < 0 ? -(long)(-v + 0.5) : (long)(v + 0.5);
}

/* "A1" (or "$A$1") as a row and column from 0; 0 when it is not one. */
static int parse_cell(const char **pp, unsigned long *row, unsigned long *col)
{
    const char *p = *pp;
    unsigned long c = 0, r = 0;
    int n = 0;
    if (*p == '$')
        p++;
    while (((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')) && n < 4) {
        c = c * 26 + (unsigned long)(owf_upper((unsigned char)*p) - 'A' + 1);
        p++;
        n++;
    }
    if (!n || n > 3 || c - 1 > MAX_COL)
        return 0;
    if (*p == '$')
        p++;
    for (n = 0; *p >= '0' && *p <= '9' && n < 8; p++, n++)
        r = r * 10 + (unsigned long)(*p - '0');
    if (!n || r < 1 || r - 1 > MAX_ROW)
        return 0;
    *row = r - 1;
    *col = c - 1;
    *pp = p;
    return 1;
}

/* "A1:C3", or "A1" for one cell */
static int parse_range(const char *s, owf_range *r)
{
    unsigned long r0, c0, r1, c1;
    if (!s || !parse_cell(&s, &r0, &c0))
        return 0;
    if (*s == ':') {
        s++;
        if (!parse_cell(&s, &r1, &c1))
            return 0;
    } else {
        r1 = r0;
        c1 = c0;
    }
    if (*s || r1 < r0 || c1 < c0)
        return 0;
    r->first_row = (unsigned)r0;
    r->last_row = (unsigned)r1;
    r->first_col = (unsigned)c0;
    r->last_col = (unsigned)c1;
    return 1;
}

static int hex_digit(int c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

/* Text as SpreadsheetML escapes it (ST_Xstring): "_x000D_" for a
 * character XML cannot carry, "_x005F_" for the "_" of a text that looks
 * like one. Decoded in place: the result is never longer. */
static size_t unescape_xstring(char *s, size_t n)
{
    size_t in = 0, out = 0;
    unsigned long high = 0;              /* a high surrogate waiting for its pair */
    while (in < n) {
        unsigned long c = 0;
        int k, hex = in + 7 <= n && s[in] == '_' && s[in + 1] == 'x' && s[in + 6] == '_';
        for (k = 2; hex && k < 6; k++) {
            int d = hex_digit((unsigned char)s[in + k]);
            if (d < 0)
                hex = 0;
            else
                c = c * 16 + (unsigned long)d;
        }
        if (!hex) {
            high = 0;
            s[out++] = s[in++];
            continue;
        }
        in += 7;
        if (c >= 0xD800 && c < 0xDC00) {
            high = c;
            continue;
        }
        if (c >= 0xDC00 && c < 0xE000) {
            if (!high)
                continue;                /* a lone half: dropped */
            c = 0x10000UL + ((high - 0xD800) << 10) + (c - 0xDC00);
        }
        high = 0;
        if (c < 0x80) {
            s[out++] = (char)c;
        } else if (c < 0x800) {
            s[out++] = (char)(0xC0 | (c >> 6));
            s[out++] = (char)(0x80 | (c & 0x3F));
        } else if (c < 0x10000) {
            s[out++] = (char)(0xE0 | (c >> 12));
            s[out++] = (char)(0x80 | ((c >> 6) & 0x3F));
            s[out++] = (char)(0x80 | (c & 0x3F));
        } else {                         /* from two escapes, 14 bytes */
            s[out++] = (char)(0xF0 | (c >> 18));
            s[out++] = (char)(0x80 | ((c >> 12) & 0x3F));
            s[out++] = (char)(0x80 | ((c >> 6) & 0x3F));
            s[out++] = (char)(0x80 | (c & 0x3F));
        }
    }
    return out;
}

/* ---- Parts and their relationships ---- */

typedef struct {
    const char *id, *type, *target;
    int external;
} rel;

typedef struct {
    rel *items;
    int count;
    owf_xml *xml;
    char dir[PART_NAME];         /* the source part's folder, "xl/" */
} rels;

static owf_xml *read_xml(const owf_unzip *zip, const char *name, int *error)
{
    unsigned char *data;
    size_t length;
    owf_xml *xml;
    int e = owf_unzip_read(zip, name, &data, &length);
    if (e != OWF_OK) {
        if (error)
            *error = e;
        return NULL;
    }
    xml = owf_xml_parse((const char *)data, length, &e);
    free(data);
    if (!xml && error)
        *error = e;
    return xml;
}

/* A target relative to dir (or from the package's root when it starts
 * with "/"), with "." and ".." worked out: "xl/worksheets/sheet1.xml".
 * 0 when it does not fit. */
static int part_name(char *out, size_t size, const char *dir, const char *target)
{
    char tmp[PART_NAME * 2];
    const char *seg[64];
    size_t segn[64], n;
    int nseg = 0, i;
    const char *p;
    if (target[0] == '/')
        n = (size_t)snprintf(tmp, sizeof tmp, "%s", target + 1);
    else
        n = (size_t)snprintf(tmp, sizeof tmp, "%s%s", dir, target);
    if (n >= sizeof tmp)
        return 0;
    for (p = tmp; *p;) {
        const char *slash = strchr(p, '/');
        size_t k = slash ? (size_t)(slash - p) : strlen(p);
        if (k == 2 && p[0] == '.' && p[1] == '.') {
            if (nseg)
                nseg--;
        } else if (k && !(k == 1 && p[0] == '.')) {
            if (nseg == 64)
                return 0;
            seg[nseg] = p;
            segn[nseg++] = k;
        }
        if (!slash)
            break;
        p = slash + 1;
    }
    n = 0;
    for (i = 0; i < nseg; i++) {
        if (n + segn[i] + 2 > size)
            return 0;
        if (i)
            out[n++] = '/';
        memcpy(out + n, seg[i], segn[i]);
        n += segn[i];
    }
    out[n] = 0;
    return nseg > 0;
}

/* The relationships of a part: "xl/workbook.xml" has
 * "xl/_rels/workbook.xml.rels". None is no error. */
static void read_rels(const owf_unzip *zip, const char *part, rels *r)
{
    const char *slash = strrchr(part, '/');
    char name[PART_NAME + 16];
    const owf_xml_node *n;
    int count = 0;

    memset(r, 0, sizeof *r);
    snprintf(r->dir, sizeof r->dir, "%.*s", slash ? (int)(slash - part + 1) : 0, part);
    snprintf(name, sizeof name, "%s_rels/%s.rels", r->dir, slash ? slash + 1 : part);
    r->xml = read_xml(zip, name, NULL);
    if (!r->xml)
        return;
    for (n = owf_xml_root(r->xml)->first; n; n = n->next)
        if (n->type == OWF_XML_ELEMENT)
            count++;
    r->items = calloc((size_t)count + 1, sizeof *r->items);
    if (!r->items)
        return;
    for (n = owf_xml_root(r->xml)->first; n; n = n->next) {
        rel *it = &r->items[r->count];
        const char *mode;
        if (n->type != OWF_XML_ELEMENT || strcmp(n->name, "Relationship"))
            continue;
        it->id = attr(n, "Id");
        it->type = attr(n, "Type");
        it->target = attr(n, "Target");
        mode = attr(n, "TargetMode");
        it->external = mode && !strcmp(mode, "External");
        if (it->id && it->type && it->target)
            r->count++;
    }
}

static void free_rels(rels *r)
{
    free(r->items);
    owf_xml_free(r->xml);
    memset(r, 0, sizeof *r);
}

static int type_is(const char *type, const char *kind)
{
    size_t n = strlen(type), k = strlen(kind);
    return n >= k && !strcmp(type + n - k, kind);
}

static const rel *rel_by_type(const rels *r, const char *kind)
{
    int i;
    for (i = 0; i < r->count; i++)
        if (type_is(r->items[i].type, kind))
            return &r->items[i];
    return NULL;
}

static const rel *rel_by_id(const rels *r, const char *id)
{
    int i;
    for (i = 0; id && i < r->count; i++)
        if (!strcmp(r->items[i].id, id))
            return &r->items[i];
    return NULL;
}

/* The part a relationship points to, inside the package. */
static int rel_part(const rels *r, const rel *it, char *out, size_t size)
{
    return it && !it->external && part_name(out, size, r->dir, it->target);
}

/* ---- The reader's state ---- */

typedef struct {
    unsigned char line[4];
    unsigned long colour[4];
} xborder;

typedef struct {
    unsigned id;
    int index;                   /* the book's */
} xnumfmt;

/* A look conditional formatting gives (a dxf): the font's parts it sets, its colour and the fill. */
typedef struct {
    unsigned set, flags;
    unsigned long colour, fill;  /* OWF_SHADE, 0 none */
} xdxf;

/* The conditional formatting being read: its ranges, the rule (an index: the array moves), what it holds. */
typedef struct {
    owf_range *ranges;
    int nranges;
    int rule;                    /* the sheet's condfmts index, -1 none */
    int in_formula, nformula;
    owf_buf text;
    int in_scale, ncfvo, ncolor;
    owf_buf xml;                 /* a kept rule's, as read */
    int keeping, skip_depth, skip_end;
} cfread;

typedef struct {
    char *name;
    int kind;                    /* 0 a worksheet, 1 a chart sheet, 2 other */
    int state;                   /* 0 visible, 1 hidden, 2 very hidden */
    char part[PART_NAME];
    int sheet;                   /* the book's, or -1 */
} xsheet;

typedef struct {
    unsigned long si;
    unsigned row, col;
    char *text;                  /* the model's, as at row, col */
} xshared;

typedef struct {
    unsigned row, col;
    unsigned long si;
} xpending;

typedef struct {
    owf_range range;
    char *text;
} xarray;

typedef struct {
    owf_book *book;
    owf_report *report;
    const owf_unzip *zip;
    char wb_part[PART_NAME];
    rels wb_rels;
    int failed;                  /* memory ran out */
    int damaged;                 /* a part was damaged or cut short */

    /* styles */
    unsigned long theme[12];
    unsigned long palette[64];
    owf_charfmt *fonts;
    int nfonts;
    unsigned long *fills;
    unsigned char *fill_approx;
    int nfills;
    xborder *borders;
    int nborders;
    xnumfmt *numfmts;
    int nnumfmts;
    int *xf_style;
    int nxf;
    xdxf *dxfs;
    int ndxfs;

    /* shared strings, each with a NUL after */
    owf_buf sst;
    size_t *sst_at;
    unsigned long nsst, capsst;

    char **books;                /* other books by number, from 1 */
    int nbooks;
    xsheet *sheets;
    int nsheets;

    /* the sheet being read */
    owf_sheet *sheet;
    xshared *shared;
    int nshared, capshared;
    xpending *pending;
    int npending, cappending;
    xarray *arrays;
    int narrays, caparrays;
    unsigned long array_budget, text_budget;
} xlsx;

static void *grow(xlsx *x, void *items, int count, int *capacity, size_t size)
{
    void *p;
    int want;
    if (count < *capacity)
        return items;
    want = *capacity ? *capacity * 2 : 8;
    p = realloc(items, (size_t)want * size);
    if (!p) {
        x->failed = 1;
        return NULL;
    }
    *capacity = want;
    return p;
}

static char *copy(xlsx *x, const char *s, size_t n)
{
    char *out = malloc(n + 1);
    if (!out) {
        x->failed = 1;
        return NULL;
    }
    memcpy(out, s, n);
    out[n] = 0;
    return out;
}

/* ---- Colours ---- */

/* The theme's colours in the order cells number them: light 1, dark 1,
 * light 2, dark 2, the six accents, the two link colours. Without a theme,
 * Office's own. */
static const unsigned long office_theme[12] = {
    0xFFFFFF, 0x000000, 0xE7E6E6, 0x44546A, 0x4472C4, 0xED7D31, 0xA5A5A5, 0xFFC000, 0x5B9BD5, 0x70AD47,
    0x0563C1, 0x954F72
};

static void read_theme(xlsx *x, const char *part)
{
    static const char *const order[12] = { "lt1", "dk1", "lt2", "dk2", "accent1", "accent2", "accent3",
                                            "accent4", "accent5", "accent6", "hlink", "folHlink" };
    owf_xml *xml = read_xml(x->zip, part, NULL);
    const owf_xml_node *n, *c;
    int i;
    if (!xml)
        return;
    n = owf_xml_root(xml);
    /* a:theme/a:themeElements/a:clrScheme */
    for (n = n->first; n && !(n->type == OWF_XML_ELEMENT && !strcmp(n->name, "themeElements")); n = n->next)
        ;
    for (n = n ? n->first : NULL; n && !(n->type == OWF_XML_ELEMENT && !strcmp(n->name, "clrScheme")); n = n->next)
        ;
    for (c = n ? n->first : NULL; c; c = c->next) {
        const owf_xml_node *v;
        if (c->type != OWF_XML_ELEMENT || (strcmp(c->ns, NS_A) && strcmp(c->ns, NS_A_STRICT)))
            continue;
        for (i = 0; i < 12 && strcmp(c->name, order[i]); i++)
            ;
        if (i == 12)
            continue;
        for (v = c->first; v && v->type != OWF_XML_ELEMENT; v = v->next)
            ;
        if (v && !strcmp(v->name, "srgbClr"))
            owf_parse_colour(attr(v, "val"), &x->theme[i]);
        else if (v && !strcmp(v->name, "sysClr"))
            owf_parse_colour(attr(v, "lastClr"), &x->theme[i]);
    }
    owf_xml_free(xml);
}

/* A colour lightened (tint above 0) or darkened, as Excel does it: on the
 * lightness of the colour in hue, lightness and saturation. */
static unsigned long tinted(unsigned long rgb, double tint)
{
    double c[3], max, min, h = 0, l, s = 0, d, q, p, t;
    int i;
    if (tint == 0)
        return rgb;
    if (tint < -1)
        tint = -1;
    if (tint > 1)
        tint = 1;
    for (i = 0; i < 3; i++)
        c[i] = (double)((rgb >> (16 - 8 * i)) & 0xFF) / 255.0;
    max = c[0] > c[1] ? (c[0] > c[2] ? c[0] : c[2]) : (c[1] > c[2] ? c[1] : c[2]);
    min = c[0] < c[1] ? (c[0] < c[2] ? c[0] : c[2]) : (c[1] < c[2] ? c[1] : c[2]);
    l = (max + min) / 2;
    d = max - min;
    if (d > 0) {
        s = l > 0.5 ? d / (2 - max - min) : d / (max + min);
        if (max == c[0])
            h = (c[1] - c[2]) / d + (c[1] < c[2] ? 6 : 0);
        else if (max == c[1])
            h = (c[2] - c[0]) / d + 2;
        else
            h = (c[0] - c[1]) / d + 4;
        h /= 6;
    }
    l = tint < 0 ? l * (1 + tint) : l * (1 - tint) + tint;
    if (s == 0) {
        c[0] = c[1] = c[2] = l;
    } else {
        q = l < 0.5 ? l * (1 + s) : l + s - l * s;
        p = 2 * l - q;
        for (i = 0; i < 3; i++) {
            t = h + (1 - i) / 3.0;
            if (t < 0)
                t += 1;
            if (t > 1)
                t -= 1;
            c[i] = t < 1 / 6.0 ? p + (q - p) * 6 * t : t < 0.5 ? q : t < 2 / 3.0 ? p + (q - p) * (2 / 3.0 - t) * 6 : p;
        }
    }
    rgb = 0;
    for (i = 0; i < 3; i++) {
        long v = rounded(c[i] * 255);
        rgb = (rgb << 8) | (unsigned long)(v < 0 ? 0 : v > 255 ? 255 : v);
    }
    return rgb;
}

/* <color rgb="FFRRGGBB"/>, indexed="10", theme="4" tint="0.4", auto="1":
 * 1 with the colour, 0 for automatic or none. */
static int colour_of(const xlsx *x, const owf_xml_node *n, unsigned long *rgb)
{
    const char *v;
    double tint = 0;
    long i;
    if (!n || xbool(attr(n, "auto"), 0))
        return 0;
    xdouble(attr(n, "tint"), &tint);
    if ((v = attr(n, "rgb")) != NULL) {
        if (strlen(v) == 8)
            v += 2;                      /* the alpha first */
        if (!owf_parse_colour(v, rgb))
            return 0;
    } else if ((i = xlong(attr(n, "theme"), -1, 0, 11)) >= 0) {
        *rgb = x->theme[i];
    } else if ((i = xlong(attr(n, "indexed"), -1, 0, 63)) >= 0) {
        *rgb = x->palette[i];
    } else {
        return 0;                        /* 64 and 65: the system's, automatic */
    }
    *rgb = tinted(*rgb, tint);
    return 1;
}

/* ---- Styles ---- */

static int numfmt_of(xlsx *x, unsigned id)
{
    const char *builtin;
    int i;
    for (i = 0; i < x->nnumfmts; i++)
        if (x->numfmts[i].id == id)
            return x->numfmts[i].index;
    builtin = owf_xl_builtin_format(id);
    if (!builtin)
        return 0;
    i = owf_book_numfmt(x->book, builtin);
    if (i < 0) {
        x->failed = 1;
        return 0;
    }
    return i;
}

static int count_children(const owf_xml_node *n, const char *name)
{
    int count = 0;
    for (n = main_child(n, name); n; n = main_next(n, name))
        count++;
    return count;
}

static void read_font(xlsx *x, const owf_xml_node *f, owf_charfmt *fmt)
{
    static const owf_font_kind kinds[6] = { OWF_FONT_ANY, OWF_FONT_SERIF, OWF_FONT_SANS,
                                            OWF_FONT_MONO, OWF_FONT_ANY, OWF_FONT_ANY };
    const owf_xml_node *c;
    const char *name = NULL, *v;
    long family = 0;
    double size;
    unsigned long rgb;
    owf_charfmt_init(fmt);
    for (c = f->first; c; c = c->next) {
        if (c->type != OWF_XML_ELEMENT || !is_main(c->ns))
            continue;
        v = attr(c, "val");
        if (!strcmp(c->name, "b") && xbool(v, 1))
            fmt->flags |= OWF_BOLD;
        else if (!strcmp(c->name, "i") && xbool(v, 1))
            fmt->flags |= OWF_ITALIC;
        else if (!strcmp(c->name, "strike") && xbool(v, 1))
            fmt->flags |= OWF_STRIKE;
        else if (!strcmp(c->name, "u") && (!v || strcmp(v, "none")))
            fmt->flags |= OWF_UNDERLINE;
        else if (!strcmp(c->name, "vertAlign") && v)
            fmt->flags |= !strcmp(v, "superscript") ? OWF_SUPER : !strcmp(v, "subscript") ? OWF_SUB : 0;
        else if (!strcmp(c->name, "sz") && xdouble(v, &size) && size > 0 && size < 4096)
            fmt->size = (int)rounded(size * OWF_TWIPS_PER_POINT);
        else if (!strcmp(c->name, "color") && colour_of(x, c, &rgb))
            fmt->colour = rgb;
        else if ((!strcmp(c->name, "name") || !strcmp(c->name, "rFont")) && v && *v)
            name = v;
        else if (!strcmp(c->name, "family"))
            family = xlong(v, 0, 0, 5);
    }
    if (name) {
        fmt->font = owf_book_font(x->book, name, kinds[family]);
        if (fmt->font < 0) {
            fmt->font = -1;
            x->failed = 1;
        }
    }
}

static int line_of(const char *style)
{
    static const char *const names[14] = { "none", "thin", "medium", "dashed", "dotted", "thick", "double", "hair",
                                           "mediumDashed", "dashDot", "mediumDashDot", "dashDotDot",
                                           "mediumDashDotDot", "slantDashDot" };
    int i;
    for (i = 0; style && i < 14; i++)
        if (!strcmp(style, names[i]))
            return i;
    return OWF_LINE_NONE;
}

static void read_border(xlsx *x, const owf_xml_node *b, xborder *out)
{
    static const char *const sides[4][2] = { { "left", "start" }, { "right", "end" }, { "top", "top" },
                                             { "bottom", "bottom" } };
    const owf_xml_node *c;
    int i, k;
    memset(out, 0, sizeof *out);
    for (c = b->first; c; c = c->next) {
        unsigned long rgb;
        int line;
        if (c->type != OWF_XML_ELEMENT || !is_main(c->ns))
            continue;
        line = line_of(attr(c, "style"));
        if (!strcmp(c->name, "diagonal")) {
            if (line && (xbool(attr(b, "diagonalUp"), 0) || xbool(attr(b, "diagonalDown"), 0)))
                owf_report_add(x->report, OWF_NOTE_LOST, "Diagonal lines across cells are not kept");
            continue;
        }
        for (i = 0; i < 4; i++)
            for (k = 0; k < 2; k++)
                if (!strcmp(c->name, sides[i][k]) && line) {
                    out->line[i] = (unsigned char)line;
                    if (colour_of(x, main_child(c, "color"), &rgb))
                        out->colour[i] = OWF_SHADE(rgb);
                }
    }
}

/* approx: 1 for a pattern, 2 for a gradient, which the model makes solid */
static unsigned long read_fill(xlsx *x, const owf_xml_node *f, unsigned char *approx)
{
    const owf_xml_node *p = main_child(f, "patternFill"), *g = main_child(f, "gradientFill");
    unsigned long rgb;
    if (p) {
        const char *type = attr(p, "patternType");
        const owf_xml_node *fg = main_child(p, "fgColor"), *bg = main_child(p, "bgColor");
        if (!type) {
            /* no pattern given: solid when a colour is (Excel's own files always say) */
            type = fg ? "solid" : "none";
        }
        if (!strcmp(type, "none"))
            return 0;
        *approx = strcmp(type, "solid") != 0;
        if (colour_of(x, fg, &rgb) || colour_of(x, bg, &rgb))
            return OWF_SHADE(rgb);
        return *approx ? 0 : OWF_SHADE(0x000000);
    }
    if (g) {
        const owf_xml_node *stop = main_child(g, "stop");
        *approx = 2;
        if (stop && colour_of(x, main_child(stop, "color"), &rgb))
            return OWF_SHADE(rgb);
    }
    return 0;
}

static void xf_style(xlsx *x, const owf_xml_node *xf, owf_cellstyle *s)
{
    static const char *const hnames[8] = { "general", "left", "center", "right", "fill", "justify",
                                           "centerContinuous", "distributed" };
    static const unsigned char hvals[8] = { OWF_HALIGN_GENERAL, OWF_HALIGN_LEFT, OWF_HALIGN_CENTRE,
                                            OWF_HALIGN_RIGHT, OWF_HALIGN_FILL, OWF_HALIGN_JUSTIFY,
                                            OWF_HALIGN_CENTRE_ACROSS, OWF_HALIGN_JUSTIFY };
    const owf_xml_node *al = main_child(xf, "alignment");
    long font = xlong(attr(xf, "fontId"), 0, 0, 0x7FFFFFFFL), fill = xlong(attr(xf, "fillId"), 0, 0, 0x7FFFFFFFL),
         border = xlong(attr(xf, "borderId"), 0, 0, 0x7FFFFFFFL);
    const char *v;
    int i;

    owf_cellstyle_init(s);
    if (font < x->nfonts)
        s->font = x->fonts[font];
    else if (x->nfonts)
        s->font = x->fonts[0];           /* no such font: the book's own */
    s->number_format = numfmt_of(x, (unsigned)xlong(attr(xf, "numFmtId"), 0, 0, 0xFFFF));
    if (fill < x->nfills) {
        s->fill = x->fills[fill];
        if (x->fill_approx[fill] == 1)
            owf_report_add(x->report, OWF_NOTE_APPROX, "Patterned cell backgrounds became solid colours");
        else if (x->fill_approx[fill] == 2)
            owf_report_add(x->report, OWF_NOTE_APPROX, "Shaded (gradient) cell backgrounds became solid colours");
    }
    if (border < x->nborders)
        for (i = 0; i < 4; i++) {
            s->border[i] = x->borders[border].line[i];
            s->border_colour[i] = x->borders[border].colour[i];
        }
    if (!al)
        return;
    if ((v = attr(al, "horizontal")) != NULL)
        for (i = 0; i < 8; i++)
            if (!strcmp(v, hnames[i]))
                s->halign = hvals[i];
    if ((v = attr(al, "vertical")) != NULL)
        s->valign = !strcmp(v, "top") || !strcmp(v, "justify") ? OWF_VALIGN_TOP
                    : !strcmp(v, "center") || !strcmp(v, "distributed") ? OWF_VALIGN_CENTRE : OWF_VALIGN_BOTTOM;
    s->wrap = (unsigned char)xbool(attr(al, "wrapText"), 0);
    s->indent = (unsigned char)xlong(attr(al, "indent"), 0, 0, 255);
    if (xlong(attr(al, "textRotation"), 0, 0, 255))
        owf_report_add(x->report, OWF_NOTE_APPROX, "Text turned at an angle in cells is shown level");
}

/* A dxf: <font><b/><i val="0"/><color .../></font>, <fill><patternFill><bgColor .../></patternFill></fill>
 * (a look's solid fill is its bgColor; its fgColor when it has none). */
static void read_dxf(xlsx *x, const owf_xml_node *d, xdxf *out)
{
    static const struct { const char *name; unsigned flag; } parts[4] = {
        { "b", OWF_BOLD }, { "i", OWF_ITALIC }, { "strike", OWF_STRIKE }, { "u", OWF_UNDERLINE }
    };
    const owf_xml_node *f = main_child(d, "font"), *c, *p;
    unsigned long rgb;
    int k;
    memset(out, 0, sizeof *out);
    for (c = f ? f->first : NULL; c; c = c->next) {
        const char *v;
        if (c->type != OWF_XML_ELEMENT || !is_main(c->ns))
            continue;
        v = attr(c, "val");
        for (k = 0; k < 4; k++)
            if (!strcmp(c->name, parts[k].name)) {
                int on = k == 3 ? !v || strcmp(v, "none") : xbool(v, 1);
                out->set |= parts[k].flag;
                if (on)
                    out->flags |= parts[k].flag;
            }
        if (!strcmp(c->name, "color") && colour_of(x, c, &rgb))
            out->colour = OWF_SHADE(rgb);
    }
    p = main_child(main_child(d, "fill"), "patternFill");
    if (p) {
        const char *type = attr(p, "patternType");
        if (!type || strcmp(type, "none")) {
            if (colour_of(x, main_child(p, "bgColor"), &rgb) || colour_of(x, main_child(p, "fgColor"), &rgb))
                out->fill = OWF_SHADE(rgb);
        }
    }
}

static void read_styles(xlsx *x, const char *part)
{
    owf_xml *xml = read_xml(x->zip, part, NULL);
    const owf_xml_node *root, *sec, *n;
    owf_cellstyle style;
    int i, count;

    if (!xml) {
        owf_report_add(x->report, OWF_NOTE_LOST, "The cells' formatting could not be read");
        return;
    }
    root = owf_xml_root(xml);
    /* the palette, when the book changes Excel's */
    if ((sec = main_child(main_child(root, "colors"), "indexedColors")) != NULL)
        for (i = 0, n = main_child(sec, "rgbColor"); n && i < 64; n = main_next(n, "rgbColor"), i++) {
            const char *v = attr(n, "rgb");
            if (v && strlen(v) == 8)
                v += 2;
            owf_parse_colour(v, &x->palette[i]);
        }
    if ((sec = main_child(root, "numFmts")) != NULL) {
        count = count_children(sec, "numFmt");
        x->numfmts = calloc((size_t)count + 1, sizeof *x->numfmts);
        if (!x->numfmts) {
            x->failed = 1;
            goto done;
        }
        for (n = main_child(sec, "numFmt"); n; n = main_next(n, "numFmt")) {
            long id = xlong(attr(n, "numFmtId"), -1, 0, 0xFFFF);
            const char *code = attr(n, "formatCode");
            int index;
            if (id < 0 || !code)
                continue;
            index = owf_book_numfmt(x->book, code);
            if (index < 0) {
                x->failed = 1;
                goto done;
            }
            x->numfmts[x->nnumfmts].id = (unsigned)id;
            x->numfmts[x->nnumfmts++].index = index;
        }
    }
    if ((sec = main_child(root, "fonts")) != NULL) {
        count = count_children(sec, "font");
        x->fonts = calloc((size_t)count + 1, sizeof *x->fonts);
        if (!x->fonts) {
            x->failed = 1;
            goto done;
        }
        for (n = main_child(sec, "font"); n; n = main_next(n, "font"))
            read_font(x, n, &x->fonts[x->nfonts++]);
    }
    if ((sec = main_child(root, "fills")) != NULL) {
        count = count_children(sec, "fill");
        x->fills = calloc((size_t)count + 1, sizeof *x->fills);
        x->fill_approx = calloc((size_t)count + 1, 1);
        if (!x->fills || !x->fill_approx) {
            x->failed = 1;
            goto done;
        }
        for (n = main_child(sec, "fill"); n; n = main_next(n, "fill")) {
            x->fills[x->nfills] = read_fill(x, n, &x->fill_approx[x->nfills]);
            x->nfills++;
        }
    }
    if ((sec = main_child(root, "borders")) != NULL) {
        count = count_children(sec, "border");
        x->borders = calloc((size_t)count + 1, sizeof *x->borders);
        if (!x->borders) {
            x->failed = 1;
            goto done;
        }
        for (n = main_child(sec, "border"); n; n = main_next(n, "border"))
            read_border(x, n, &x->borders[x->nborders++]);
    }
    if ((sec = main_child(root, "dxfs")) != NULL) {
        count = count_children(sec, "dxf");
        x->dxfs = calloc((size_t)count + 1, sizeof *x->dxfs);
        if (!x->dxfs) {
            x->failed = 1;
            goto done;
        }
        for (n = main_child(sec, "dxf"); n; n = main_next(n, "dxf"))
            read_dxf(x, n, &x->dxfs[x->ndxfs++]);
    }
    if ((sec = main_child(root, "cellXfs")) != NULL) {
        count = count_children(sec, "xf");
        x->xf_style = calloc((size_t)count + 1, sizeof *x->xf_style);
        if (!x->xf_style) {
            x->failed = 1;
            goto done;
        }
        for (n = main_child(sec, "xf"); n && !x->failed; n = main_next(n, "xf")) {
            xf_style(x, n, &style);
            if (!x->nxf)
                x->book->styles[0] = style;      /* the first is the default look */
            x->xf_style[x->nxf] = owf_book_style(x->book, &style);
            if (x->xf_style[x->nxf] < 0) {
                x->xf_style[x->nxf] = 0;
                x->failed = 1;
            }
            x->nxf++;
        }
    }
done:
    owf_xml_free(xml);
}

static int style_of(const xlsx *x, const char *s)
{
    long i = xlong(s, 0, 0, 0x7FFFFFFFL);
    return i < x->nxf ? x->xf_style[i] : 0;
}

/* ---- Text in <t>, <r>: its runs, without the phonetic guide ---- */

/* Shared strings: <sst><si><t>..</t></si> or <si><r><t>..</t></r>..</si>.
 * Each string's text goes into x->sst with a NUL after. */
static int read_shared_strings(xlsx *x, const char *part)
{
    unsigned char *data;
    size_t length;
    owf_xml_scan *scan;
    owf_xml_event e;
    int r, in_t = 0, in_si = 0, phonetic = 0, rich = 0;
    size_t start = 0;

    r = owf_unzip_read(x->zip, part, &data, &length);
    if (r != OWF_OK) {
        if (r == OWF_ERR_MEMORY)
            x->failed = 1;
        else
            x->damaged = 1;
        return r;
    }
    scan = owf_xml_scan_new((char *)data, length);
    if (!scan) {
        free(data);
        x->failed = 1;
        return OWF_ERR_MEMORY;
    }
    while ((r = owf_xml_scan_next(scan, &e)) > 0 && !x->failed) {
        if (e.type == OWF_XS_TEXT) {
            if (in_t && !phonetic)
                owf_buf_put(&x->sst, e.text, e.length);
            continue;
        }
        if (!is_main(e.ns))
            continue;
        if (e.type == OWF_XS_START) {
            if (!strcmp(e.name, "si")) {
                in_si = 1;
                start = x->sst.length;
            } else if (!strcmp(e.name, "t") && in_si) {
                in_t = 1;
            } else if (!strcmp(e.name, "rPh")) {
                phonetic = 1;
            } else if (!strcmp(e.name, "r") && in_si) {
                rich = 1;
            }
        } else if (!strcmp(e.name, "t")) {
            in_t = 0;
        } else if (!strcmp(e.name, "rPh")) {
            phonetic = 0;
        } else if (!strcmp(e.name, "si") && in_si) {
            size_t n;
            in_si = 0;
            if (x->sst.failed)
                break;
            n = unescape_xstring((char *)x->sst.data + start, x->sst.length - start);
            x->sst.length = start + n;
            owf_buf_putc(&x->sst, 0);
            if (x->nsst == x->capsst) {
                unsigned long want = x->capsst ? x->capsst * 2 : 256;
                size_t *p = realloc(x->sst_at, want * sizeof *p);
                if (!p) {
                    x->failed = 1;
                    break;
                }
                x->sst_at = p;
                x->capsst = want;
            }
            x->sst_at[x->nsst++] = start;
        }
    }
    if (r < 0) {
        if (owf_xml_scan_error(scan) == OWF_ERR_MEMORY)
            x->failed = 1;
        else
            x->damaged = 1;
    }
    if (x->sst.failed)
        x->failed = 1;
    if (rich)
        owf_report_add(x->report, OWF_NOTE_APPROX, "Text formatted in parts within a cell takes the cell's formatting");
    owf_xml_scan_free(scan);
    free(data);
    return OWF_OK;
}

/* ---- A worksheet ---- */

static void set_text(xlsx *x, owf_cell *c, const char *s, size_t n)
{
    if (owf_utf8_valid((const unsigned char *)s, n)) {
        if (owf_cell_set_text(c, s, n) < 0)
            x->failed = 1;
    } else {
        owf_buf b;
        owf_buf_init(&b);
        owf_buf_put_utf8_clean(&b, s, n);
        if (b.failed || owf_cell_set_text(c, b.data ? (const char *)b.data : "", b.length) < 0)
            x->failed = 1;
        owf_buf_free(&b);
    }
}

static void set_formula(xlsx *x, owf_cell *c, char *model_text)
{
    if (!model_text) {
        x->failed = 1;
        return;
    }
    if (owf_cell_set_formula(c, model_text) < 0)
        x->failed = 1;
}

/* A shared formula's text as at row, col: the first cell's, moved. */
static char *shared_at(xlsx *x, const xshared *s, unsigned row, unsigned col)
{
    char *t = owf_xl_formula_move(s->text, (long)row - (long)s->row, (long)col - (long)s->col);
    if (!t)
        x->failed = 1;
    return t;
}

/* The shared formula numbered si: they come in order, so this looks
 * from the end. */
static const xshared *find_shared(const xlsx *x, unsigned long si)
{
    int lo = 0, hi = x->nshared;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (x->shared[mid].si < si)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo < x->nshared && x->shared[lo].si == si ? &x->shared[lo] : NULL;
}

static void add_shared(xlsx *x, unsigned long si, unsigned row, unsigned col, const char *text)
{
    xshared *s;
    int at;
    if (find_shared(x, si))
        return;                          /* a second first cell: the first one counts */
    if (!(s = grow(x, x->shared, x->nshared, &x->capshared, sizeof *s)))
        return;
    x->shared = s;
    for (at = x->nshared; at > 0 && x->shared[at - 1].si > si; at--)
        ;
    memmove(&x->shared[at + 1], &x->shared[at], (size_t)(x->nshared - at) * sizeof *s);
    s = &x->shared[at];
    s->si = si;
    s->row = row;
    s->col = col;
    if (!(s->text = copy(x, text, strlen(text))))
        return;
    x->nshared++;
}

typedef struct {
    /* the cell being read */
    unsigned long row, col;
    int style;
    char type[16];
    int has_v, has_f, in_v, in_f, in_is, in_t, phonetic;
    char ftype[16];
    char fref[40];
    long si;
    owf_buf v, f, is;
    /* where the next cell goes without an r */
    unsigned long next_row, next_col;
    int in_data;
} cellstate;

static void commit_cell(xlsx *x, cellstate *cs)
{
    owf_cell *c = owf_sheet_cell(x->sheet, (unsigned)cs->row, (unsigned)cs->col, 1);
    const char *v = cs->v.length ? (const char *)cs->v.data : "";   /* the buffer is the last cell's when empty */
    double d;
    char *text;

    if (!c) {
        x->failed = 1;
        return;
    }
    c->style = cs->style;
    if (cs->v.failed || cs->f.failed || cs->is.failed) {
        x->failed = 1;
        return;
    }
    if (!strcmp(cs->type, "s")) {
        long i = xlong(v, -1, 0, 0x7FFFFFFFL);
        const char *t = cs->has_v && i >= 0 && (unsigned long)i < x->nsst ? (const char *)x->sst.data + x->sst_at[i] : "";
        size_t n = strlen(t);
        if (n > x->text_budget) {
            owf_report_add(x->report, OWF_NOTE_LOST, "Some cells' text was left out: the shared strings were used too often");
            n = 0;
        }
        x->text_budget -= n;
        set_text(x, c, t, n);
    } else if (!strcmp(cs->type, "inlineStr")) {
        if (cs->is.length)
            set_text(x, c, (const char *)cs->is.data, unescape_xstring((char *)cs->is.data, cs->is.length));
        else
            set_text(x, c, "", 0);
    } else if (!strcmp(cs->type, "str")) {
        if (cs->v.length)
            set_text(x, c, v, unescape_xstring((char *)cs->v.data, cs->v.length));
        else
            set_text(x, c, "", 0);
    } else if (!strcmp(cs->type, "b")) {
        if (cs->has_v)
            owf_cell_set_bool(c, xbool(v, 0));
    } else if (!strcmp(cs->type, "e")) {
        if (cs->has_v) {
            int code = owf_xl_error_code(v, strlen(v));
            if (code < 0) {
                owf_report_add(x->report, OWF_NOTE_APPROX, "Error values the model does not have (#SPILL!, #CALC!...) became #N/A");
                code = OWF_ERROR_NA;
            }
            owf_cell_set_error(c, code);
        }
    } else if (!strcmp(cs->type, "d")) {
        const char *s = v;
        if (*s == 'T') {                 /* a time alone */
            char day[64];
            snprintf(day, sizeof day, "1899-12-30%.40s", s);
            if (owf_parse_iso_date(day, &d))
                owf_cell_set_number(c, d);
        } else if (cs->has_v && owf_parse_iso_date(s, &d)) {
            owf_cell_set_number(c, d - (x->book->date1904 ? 1462 : 0));
        }
    } else if (cs->has_v && *v) {
        if (owf_number_parse(v, strlen(v), &d))
            owf_cell_set_number(c, d);
        else
            owf_report_add(x->report, OWF_NOTE_LOST, "Some cells' numbers could not be read");
    }
    if (!cs->has_f)
        return;
    /* the formula */
    if (!strcmp(cs->ftype, "dataTable")) {
        owf_report_add(x->report, OWF_NOTE_LOST, "Data tables keep their values; their formulas are not read yet");
        return;
    }
    if (!strcmp(cs->ftype, "shared")) {
        const xshared *s;
        if (cs->si < 0)
            return;
        if (cs->f.length) {
            text = owf_xl_formula_from_file((const char *)cs->f.data, x->books, x->nbooks);
            if (!text) {
                x->failed = 1;
                return;
            }
            add_shared(x, (unsigned long)cs->si, (unsigned)cs->row, (unsigned)cs->col, text);
            set_formula(x, c, text);
            free(text);
            return;
        }
        if ((s = find_shared(x, (unsigned long)cs->si)) != NULL) {
            text = shared_at(x, s, (unsigned)cs->row, (unsigned)cs->col);
            set_formula(x, c, text);
            free(text);
        } else {                         /* its first cell is still to come */
            xpending *p = grow(x, x->pending, x->npending, &x->cappending, sizeof *p);
            if (!p)
                return;
            x->pending = p;
            p = &x->pending[x->npending++];
            p->row = (unsigned)cs->row;
            p->col = (unsigned)cs->col;
            p->si = (unsigned long)cs->si;
        }
        return;
    }
    if (!cs->f.length)
        return;
    text = owf_xl_formula_from_file((const char *)cs->f.data, x->books, x->nbooks);
    set_formula(x, c, text);
    if (text && !strcmp(cs->ftype, "array")) {
        owf_range range;
        c->flags |= OWF_CELL_ARRAY;
        if (parse_range(cs->fref, &range) && (range.last_row > range.first_row || range.last_col > range.first_col)) {
            if (x->narrays < MAX_ARRAYS) {
                xarray *a = grow(x, x->arrays, x->narrays, &x->caparrays, sizeof *a);
                if (a) {
                    x->arrays = a;
                    a = &x->arrays[x->narrays];
                    a->range = range;
                    if ((a->text = copy(x, text, strlen(text))) != NULL)
                        x->narrays++;
                }
            } else {
                owf_report_add(x->report, OWF_NOTE_LOST, "Some array formulas keep only their first cell's formula");
            }
        }
    }
    free(text);
}

/* After the sheet: shared formulas whose first cell came late, and array
 * formulas on every cell of their ranges, as the model keeps them. */
static void finish_sheet(xlsx *x)
{
    unsigned long *budget = &x->array_budget;
    int i, k;
    for (i = 0; i < x->npending && !x->failed; i++) {
        const xpending *p = &x->pending[i];
        const xshared *s = find_shared(x, p->si);
        owf_cell *c = owf_sheet_cell(x->sheet, p->row, p->col, 0);
        char *text;
        if (!s || !c) {
            owf_report_add(x->report, OWF_NOTE_LOST, "Some formulas could not be read; their cells keep their values");
            continue;
        }
        text = shared_at(x, s, p->row, p->col);
        set_formula(x, c, text);
        free(text);
    }
    for (i = 0; i < x->narrays && !x->failed; i++) {
        const xarray *a = &x->arrays[i];
        owf_sheet *sh = x->sheet;
        unsigned long area = (unsigned long)(a->range.last_row - a->range.first_row + 1) *
                             (a->range.last_col - a->range.first_col + 1);
        int lo = 0, hi = sh->ncells;
        if (area <= *budget) {           /* cells the file left out are made, empty, up to a budget */
            *budget -= area;
            unsigned r, col;
            for (r = a->range.first_row; r <= a->range.last_row && !x->failed; r++)
                for (col = a->range.first_col; col <= a->range.last_col; col++)
                    if (!owf_sheet_cell(sh, r, col, 1)) {
                        x->failed = 1;
                        break;
                    }
            hi = sh->ncells;
        }
        while (lo < hi) {                /* the first cell of the range's first row */
            int mid = (lo + hi) / 2;
            if (sh->cells[mid].row < a->range.first_row)
                lo = mid + 1;
            else
                hi = mid;
        }
        for (k = lo; k < sh->ncells && sh->cells[k].row <= a->range.last_row && !x->failed; k++) {
            owf_cell *c = &sh->cells[k];
            if (c->col < a->range.first_col || c->col > a->range.last_col || c->formula)
                continue;
            set_formula(x, c, (char *)a->text);
            c->flags |= OWF_CELL_ARRAY;
        }
    }
}

static void clear_sheet_state(xlsx *x)
{
    int i;
    for (i = 0; i < x->nshared; i++)
        free(x->shared[i].text);
    for (i = 0; i < x->narrays; i++)
        free(x->arrays[i].text);
    x->nshared = x->npending = x->narrays = 0;
}

static void sheet_element(xlsx *x, const owf_xml_event *e, cellstate *cs)
{
    owf_sheet *sh = x->sheet;
    const char *v;
    double d;
    const char *name = e->name;

    if (!strcmp(name, "sheetData")) {
        cs->in_data = 1;
    } else if (!strcmp(name, "row") && cs->in_data) {
        long r = xlong(ev_attr(e, "r"), -1, 1, (long)MAX_ROW + 1);
        int hidden = xbool(ev_attr(e, "hidden"), 0), custom = xbool(ev_attr(e, "customHeight"), 0);
        cs->next_row = r > 0 ? (unsigned long)(r - 1) : cs->next_row;
        cs->next_col = 0;
        if (xdouble(ev_attr(e, "ht"), &d) && d >= 0 && d < 10000) {
            int height = (int)rounded(d * OWF_TWIPS_PER_POINT);
            if (custom || hidden || height != sh->default_height)
                if (owf_sheet_set_row(sh, (unsigned)cs->next_row, height, hidden) < 0)
                    x->failed = 1;
        } else if (hidden) {
            if (owf_sheet_set_row(sh, (unsigned)cs->next_row, sh->default_height, 1) < 0)
                x->failed = 1;
        }
        if (xbool(ev_attr(e, "customFormat"), 0) && xlong(ev_attr(e, "s"), 0, 0, 0x7FFFFFFFL))
            owf_report_add(x->report, OWF_NOTE_APPROX, "Formatting of whole rows is kept on their cells only");
    } else if (!strcmp(name, "c") && cs->in_data) {
        const char *ref = ev_attr(e, "r"), *t = ev_attr(e, "t");
        unsigned long row, col;
        if (ref && parse_cell(&ref, &row, &col) && !*ref) {
            cs->row = row;
            cs->col = col;
        } else {
            cs->row = cs->next_row;
            cs->col = cs->next_col;
        }
        cs->next_row = cs->row;
        cs->next_col = cs->col + 1;
        cs->style = style_of(x, ev_attr(e, "s"));
        snprintf(cs->type, sizeof cs->type, "%s", t ? t : "n");
        cs->has_v = cs->has_f = cs->in_v = cs->in_f = cs->in_is = cs->in_t = cs->phonetic = 0;
        cs->v.length = cs->f.length = cs->is.length = 0;
        cs->ftype[0] = cs->fref[0] = 0;
        cs->si = -1;
    } else if (!strcmp(name, "v")) {
        cs->in_v = cs->has_v = 1;
    } else if (!strcmp(name, "f")) {
        cs->in_f = cs->has_f = 1;
        snprintf(cs->ftype, sizeof cs->ftype, "%s", (v = ev_attr(e, "t")) ? v : "normal");
        snprintf(cs->fref, sizeof cs->fref, "%s", (v = ev_attr(e, "ref")) ? v : "");
        cs->si = xlong(ev_attr(e, "si"), -1, 0, 0x7FFFFFFFL);
    } else if (!strcmp(name, "is")) {
        cs->in_is = 1;
    } else if (!strcmp(name, "t") && cs->in_is) {
        cs->in_t = 1;
    } else if (!strcmp(name, "r") && cs->in_is) {
        owf_report_add(x->report, OWF_NOTE_APPROX, "Text formatted in parts within a cell takes the cell's formatting");
    } else if (!strcmp(name, "rPh")) {
        cs->phonetic = 1;
    } else if (!strcmp(name, "sheetFormatPr")) {
        if (xdouble(ev_attr(e, "defaultRowHeight"), &d) && d > 0 && d < 10000)
            sh->default_height = (int)rounded(d * OWF_TWIPS_PER_POINT);
        if (xdouble(ev_attr(e, "defaultColWidth"), &d) && d >= 0 && d < 1000)
            sh->default_width = (int)rounded(d * 105);
        else if (xdouble(ev_attr(e, "baseColWidth"), &d) && d >= 0 && d < 1000) {
            long base = rounded(d);         /* characters, a margin of 5 pixels, to a multiple of 8 */
            sh->default_width = (int)(((base * 7 + 5 + 7) / 8 * 8) * 15);
        }
    } else if (!strcmp(name, "col")) {
        long first = xlong(ev_attr(e, "min"), 0, 1, 16384), last = xlong(ev_attr(e, "max"), 0, 1, 0x7FFFFFFFL);
        int width = sh->default_width;
        if (first && last >= first) {
            if (last > 16384)
                last = 16384;
            if (xdouble(ev_attr(e, "width"), &d) && d >= 0 && d < 1000)
                width = (int)rounded(d * 105);
            if (owf_sheet_set_columns(sh, (unsigned)first - 1, (unsigned)last - 1, width,
                                      style_of(x, ev_attr(e, "style")), xbool(ev_attr(e, "hidden"), 0)) < 0)
                x->failed = 1;
        }
    } else if (!strcmp(name, "pane")) {
        const char *state = ev_attr(e, "state");
        if (state && (!strcmp(state, "frozen") || !strcmp(state, "frozenSplit"))) {
            sh->freeze_cols = (unsigned)xlong(ev_attr(e, "xSplit"), 0, 0, 16384);
            sh->freeze_rows = (unsigned)xlong(ev_attr(e, "ySplit"), 0, 0, 1048576L);
        } else if (state && !strcmp(state, "split")) {
            owf_report_add(x->report, OWF_NOTE_LOST, "Windows split in two are shown whole");
        }
    } else if (!strcmp(name, "mergeCell")) {
        owf_range r;
        if (parse_range(ev_attr(e, "ref"), &r) && owf_sheet_merge(sh, &r) < 0)
            x->failed = 1;
    } else if (!strcmp(name, "dataValidation")) {
        owf_report_add(x->report, OWF_NOTE_LOST, "Data validation (lists to pick from, limits) is not read yet");
    } else if (!strcmp(name, "hyperlink")) {
        owf_report_add(x->report, OWF_NOTE_LOST, "Links in cells keep their text; where they go is not read yet");
    } else if (!strcmp(name, "drawing")) {
        owf_report_add(x->report, OWF_NOTE_LOST, "Pictures, charts and drawings on sheets are not read yet");
    } else if (!strcmp(name, "tablePart")) {
        owf_report_add(x->report, OWF_NOTE_LOST, "Tables keep their cells; the tables themselves (names, styles, filters) are not read yet");
    } else if (!strcmp(name, "autoFilter") && e->depth == 2) {
        owf_report_add(x->report, OWF_NOTE_LOST, "AutoFilter buttons are not read yet");
    } else if (!strcmp(name, "sheetProtection")) {
        owf_report_add(x->report, OWF_NOTE_INFO, "Sheet protection is not kept: the sheets open unprotected");
    }
}

/* ---- Conditional formatting ---- */

#define NS_X14 "http://schemas.microsoft.com/office/spreadsheetml/2009/9/main"

/* "A1:B2 C3" (a sqref): its ranges, from malloc. The count; 0 when none. */
static int parse_sqref(xlsx *x, const char *s, owf_range **out)
{
    int n = 0, cap = 0;
    owf_range *r = NULL;
    *out = NULL;
    while (s && *s) {
        char one[40];
        size_t k = 0;
        while (*s == ' ')
            s++;
        while (s[k] && s[k] != ' ')
            k++;
        if (k && k < sizeof one) {
            owf_range range;
            memcpy(one, s, k);
            one[k] = 0;
            if (parse_range(one, &range)) {
                owf_range *p = grow(x, r, n, &cap, sizeof *r);
                if (!p)
                    break;
                r = p;
                r[n++] = range;
            }
        }
        s += k;
    }
    if (!n)
        free(r);
    else
        *out = r;
    return n;
}

/* A colour from an element's attributes (as colour_of from a node's). */
static int ev_colour(const xlsx *x, const owf_xml_event *e, unsigned long *rgb)
{
    const char *v;
    double tint = 0;
    long i;
    if (xbool(ev_attr(e, "auto"), 0))
        return 0;
    xdouble(ev_attr(e, "tint"), &tint);
    if ((v = ev_attr(e, "rgb")) != NULL) {
        if (strlen(v) == 8)
            v += 2;
        if (!owf_parse_colour(v, rgb))
            return 0;
    } else if ((i = xlong(ev_attr(e, "theme"), -1, 0, 11)) >= 0) {
        *rgb = x->theme[i];
    } else if ((i = xlong(ev_attr(e, "indexed"), -1, 0, 63)) >= 0) {
        *rgb = x->palette[i];
    } else {
        return 0;
    }
    *rgb = tinted(*rgb, tint);
    return 1;
}

static void put_escaped(owf_buf *b, const char *s, size_t n, int in_attr)
{
    size_t i;
    for (i = 0; i < n; i++)
        switch (s[i]) {
        case '&': owf_buf_puts(b, "&amp;"); break;
        case '<': owf_buf_puts(b, "&lt;"); break;
        case '>': owf_buf_puts(b, "&gt;"); break;
        case '"': owf_buf_puts(b, in_attr ? "&quot;" : "\""); break;
        default: owf_buf_putc(b, s[i]); break;
        }
}

/* A start tag of a kept rule as read: its attributes in no namespace (a rule's dxfId and priority left
 * out: the writer gives its own). */
static void put_start_tag(owf_buf *b, const owf_xml_event *e, int rule)
{
    int i;
    owf_buf_putc(b, '<');
    owf_buf_puts(b, e->name);
    for (i = 0; i < e->nattrs; i++) {
        const owf_xml_attribute *a = &e->attrs[i];
        if (a->ns && *a->ns)
            continue;
        if (rule && (!strcmp(a->name, "dxfId") || !strcmp(a->name, "priority")))
            continue;
        owf_buf_printf(b, " %s=\"", a->name);
        put_escaped(b, a->value, strlen(a->value), 1);
        owf_buf_putc(b, '"');
    }
    owf_buf_puts(b, e->empty ? "/>" : ">");
}

static owf_condfmt *cf_rule(xlsx *x, cfread *cr)
{
    return cr->rule >= 0 && cr->rule < x->sheet->ncondfmts ? &x->sheet->condfmts[cr->rule] : NULL;
}

static void cf_formula_done(xlsx *x, cfread *cr)
{
    owf_condfmt *cf = cf_rule(x, cr);
    char *text;
    cr->in_formula = 0;
    owf_buf_putc(&cr->text, 0);
    if (cr->text.failed) {
        x->failed = 1;
        return;
    }
    if (!cf || cr->nformula >= 2)
        return;
    text = owf_xl_formula_from_file((const char *)cr->text.data, x->books, x->nbooks);
    if (!text)
        x->failed = 1;
    else
        cf->formula[cr->nformula++] = text;
}

static void cf_kept_done(xlsx *x, cfread *cr)
{
    owf_condfmt *cf = cf_rule(x, cr);
    cr->keeping = 0;
    if (cr->xml.failed || !cf) {
        x->failed |= cr->xml.failed;
        return;
    }
    if (!(cf->xml = copy(x, (const char *)cr->xml.data, cr->xml.length)))
        return;
    if (!cf->formula[0])
        owf_report_add(x->report, OWF_NOTE_APPROX,
                       "Some conditional formats (data bars, icon sets, top 10 and the like) are kept but not shown");
}

/* The events of a sheet's conditional formatting (and those of a kept rule, all of them): 1 when it took
 * the event. */
static int cf_event(xlsx *x, const owf_xml_event *e, cfread *cr)
{
    owf_condfmt *cf;
    const char *v;
    if (cr->keeping) {
        if (cr->skip_depth) {
            if (e->type == OWF_XS_END && e->depth == cr->skip_depth)
                cr->skip_depth = 0;
            return 1;
        }
        if (e->type == OWF_XS_TEXT) {
            put_escaped(&cr->xml, e->text, e->length, 0);
            if (cr->in_formula)
                owf_buf_put(&cr->text, e->text, e->length);
        } else if (e->type == OWF_XS_START) {
            if (!is_main(e->ns) || !strcmp(e->name, "extLst")) {   /* Excel 2010's extensions: left out */
                if (e->empty)
                    cr->skip_end = 1;
                else
                    cr->skip_depth = e->depth;
                return 1;
            }
            put_start_tag(&cr->xml, e, 0);
            if (e->empty)
                cr->skip_end = 1;
            if (!strcmp(e->name, "formula")) {
                cr->in_formula = 1;
                cr->text.length = 0;
            }
        } else if (cr->skip_end) {
            cr->skip_end = 0;
        } else {
            owf_buf_printf(&cr->xml, "</%s>", e->name);
            if (!strcmp(e->name, "formula") && cr->in_formula)
                cf_formula_done(x, cr);
            else if (!strcmp(e->name, "cfRule")) {
                cf_kept_done(x, cr);
                cr->rule = -1;
            }
        }
        return 1;
    }
    if (e->type == OWF_XS_TEXT) {
        if (cr->in_formula)
            owf_buf_put(&cr->text, e->text, e->length);
        return cr->in_formula;
    }
    if (e->type == OWF_XS_START && !strcmp(e->name, "conditionalFormatting") && !strcmp(e->ns, NS_X14)) {
        owf_report_add(x->report, OWF_NOTE_LOST, "Excel 2010's own conditional formats (in the sheet's extensions) are not kept");
        return 1;
    }
    if (!is_main(e->ns))
        return 0;
    if (e->type == OWF_XS_END) {
        if (!strcmp(e->name, "conditionalFormatting")) {
            free(cr->ranges);
            cr->ranges = NULL;
            cr->nranges = 0;
            cr->rule = -1;
            return 1;
        }
        if (!cr->nranges)
            return 0;
        if (!strcmp(e->name, "formula") && cr->in_formula)
            cf_formula_done(x, cr);
        else if (!strcmp(e->name, "colorScale")) {
            cr->in_scale = 0;
            if ((cf = cf_rule(x, cr)) != NULL)
                cf->npoints = cr->ncfvo < cr->ncolor ? cr->ncfvo : cr->ncolor;
        } else if (!strcmp(e->name, "cfRule"))
            cr->rule = -1;
        return 1;
    }
    if (!strcmp(e->name, "conditionalFormatting") && e->depth == 2) {
        free(cr->ranges);
        cr->nranges = parse_sqref(x, ev_attr(e, "sqref"), &cr->ranges);
        cr->rule = -1;
        return 1;
    }
    if (!cr->nranges)
        return 0;
    if (!strcmp(e->name, "cfRule")) {
        long dxf = xlong(ev_attr(e, "dxfId"), -1, 0, 0x7FFFFFFFL);
        if (!(cf = owf_sheet_add_condfmt(x->sheet))) {
            x->failed = 1;
            return 1;
        }
        cr->rule = x->sheet->ncondfmts - 1;
        cr->nformula = cr->ncfvo = cr->ncolor = cr->in_scale = 0;
        if (!(cf->ranges = malloc((size_t)cr->nranges * sizeof *cf->ranges))) {
            x->failed = 1;
            return 1;
        }
        memcpy(cf->ranges, cr->ranges, (size_t)cr->nranges * sizeof *cf->ranges);
        cf->nranges = cr->nranges;
        cf->priority = (int)xlong(ev_attr(e, "priority"), 0, 0, 0x7FFFFFFFL);
        cf->stop = xbool(ev_attr(e, "stopIfTrue"), 0);
        if (dxf >= 0 && dxf < x->ndxfs) {
            cf->font_set = x->dxfs[dxf].set;
            cf->font_flags = x->dxfs[dxf].flags;
            cf->font_colour = x->dxfs[dxf].colour;
            cf->fill = x->dxfs[dxf].fill;
        }
        v = ev_attr(e, "type");
        if (v && !strcmp(v, "cellIs")) {
            static const char *const ops[8] = { "between", "notBetween", "equal", "notEqual", "greaterThan", "lessThan",
                                                "greaterThanOrEqual", "lessThanOrEqual" };
            const char *op = ev_attr(e, "operator");
            int k;
            cf->type = OWF_CF_CELL_IS;
            cf->op = OWF_CF_EQUAL;
            for (k = 0; op && k < 8; k++)
                if (!strcmp(op, ops[k]))
                    cf->op = k;
        } else if (v && !strcmp(v, "expression")) {
            cf->type = OWF_CF_EXPRESSION;
        } else if (v && !strcmp(v, "colorScale")) {
            cf->type = OWF_CF_COLOUR_SCALE;
        } else {
            cf->type = OWF_CF_KEPT;
            cr->xml.length = 0;
            put_start_tag(&cr->xml, e, 1);
            cr->keeping = 1;
            if (e->empty) {
                cf_kept_done(x, cr);
                cr->rule = -1;
            }
        }
        return 1;
    }
    if (cr->rule < 0)
        return 1;                                    /* inside conditionalFormatting, not a rule */
    cf = cf_rule(x, cr);
    if (!strcmp(e->name, "formula")) {
        cr->in_formula = 1;
        cr->text.length = 0;
    } else if (!strcmp(e->name, "colorScale")) {
        cr->in_scale = 1;
    } else if (cr->in_scale && !strcmp(e->name, "cfvo") && cr->ncfvo < 3 && cf) {
        static const char *const kinds[6] = { "min", "max", "num", "percent", "percentile", "formula" };
        owf_cfpoint *p = &cf->points[cr->ncfvo++];
        const char *type = ev_attr(e, "type"), *val = ev_attr(e, "val");
        int k;
        p->kind = OWF_CFV_MIN;
        for (k = 0; type && k < 6; k++)
            if (!strcmp(type, kinds[k]))
                p->kind = k;
        if (type && !strcmp(type, "autoMax"))
            p->kind = OWF_CFV_MAX;
        if (val && (p->kind == OWF_CFV_FORMULA ? !!(p->value = owf_xl_formula_from_file(val, x->books, x->nbooks))
                                                : !!(p->value = copy(x, val, strlen(val)))) == 0)
            x->failed = 1;
    } else if (cr->in_scale && !strcmp(e->name, "color") && cr->ncolor < 3 && cf) {
        unsigned long rgb = 0;
        ev_colour(x, e, &rgb);
        cf->points[cr->ncolor++].colour = OWF_SHADE(rgb);
    }
    return 1;
}

static void read_sheet_notes(xlsx *x, const char *part)
{
    rels r;
    read_rels(x->zip, part, &r);
    if (rel_by_type(&r, "/comments"))
        owf_report_add(x->report, OWF_NOTE_LOST, "Comments on cells are not read yet");
    if (rel_by_type(&r, "/pivotTable"))
        owf_report_add(x->report, OWF_NOTE_LOST, "Pivot tables keep their cells; the pivot tables themselves are not read yet");
    if (rel_by_type(&r, "/threadedComment"))
        owf_report_add(x->report, OWF_NOTE_LOST, "Comments on cells are not read yet");
    free_rels(&r);
}

static void read_worksheet(xlsx *x, xsheet *xs)
{
    unsigned char *data;
    size_t length;
    owf_xml_scan *scan;
    owf_xml_event e;
    cellstate cs;
    cfread cr;
    int r;

    x->sheet = &x->book->sheets[xs->sheet];
    r = owf_unzip_read(x->zip, xs->part, &data, &length);
    if (r != OWF_OK) {
        if (r == OWF_ERR_MEMORY)
            x->failed = 1;
        else
            owf_report_add(x->report, OWF_NOTE_LOST, "A sheet could not be read: its part of the file is missing or damaged");
        return;
    }
    read_sheet_notes(x, xs->part);
    scan = owf_xml_scan_new((char *)data, length);
    if (!scan) {
        free(data);
        x->failed = 1;
        return;
    }
    memset(&cs, 0, sizeof cs);
    owf_buf_init(&cs.v);
    owf_buf_init(&cs.f);
    owf_buf_init(&cs.is);
    memset(&cr, 0, sizeof cr);
    cr.rule = -1;
    owf_buf_init(&cr.text);
    owf_buf_init(&cr.xml);
    while (!x->failed && (r = owf_xml_scan_next(scan, &e)) > 0) {
        if (!cs.in_data && cf_event(x, &e, &cr))
            continue;
        if (e.type == OWF_XS_TEXT) {
            if (cs.in_v)
                owf_buf_put(&cs.v, e.text, e.length);
            else if (cs.in_f)
                owf_buf_put(&cs.f, e.text, e.length);
            else if (cs.in_t && !cs.phonetic)
                owf_buf_put(&cs.is, e.text, e.length);
            continue;
        }
        if (!is_main(e.ns))
            continue;
        if (e.type == OWF_XS_START) {
            sheet_element(x, &e, &cs);
            continue;
        }
        if (!strcmp(e.name, "v"))
            cs.in_v = 0;
        else if (!strcmp(e.name, "f"))
            cs.in_f = 0;
        else if (!strcmp(e.name, "t"))
            cs.in_t = 0;
        else if (!strcmp(e.name, "is"))
            cs.in_is = 0;
        else if (!strcmp(e.name, "rPh"))
            cs.phonetic = 0;
        else if (!strcmp(e.name, "c") && cs.in_data)
            commit_cell(x, &cs);
        else if (!strcmp(e.name, "row"))
            cs.next_row++;
        else if (!strcmp(e.name, "sheetData"))
            cs.in_data = 0;
    }
    if (r < 0) {
        if (owf_xml_scan_error(scan) == OWF_ERR_MEMORY)
            x->failed = 1;
        else
            x->damaged = 1;
    }
    finish_sheet(x);
    clear_sheet_state(x);
    free(cr.ranges);
    owf_buf_free(&cr.text);
    owf_buf_free(&cr.xml);
    owf_buf_free(&cs.v);
    owf_buf_free(&cs.f);
    owf_buf_free(&cs.is);
    owf_xml_scan_free(scan);
    free(data);
}

/* ---- The workbook ---- */

/* Another book's file name, from an externalLink part: the last part of
 * its path. NULL for a link that is not to a book (DDE, OLE). */
static char *external_book(xlsx *x, const char *part)
{
    owf_xml *xml = read_xml(x->zip, part, NULL);
    const owf_xml_node *eb;
    const rel *it;
    rels r;
    char *name = NULL;
    if (!xml)
        return NULL;
    eb = main_child(owf_xml_root(xml), "externalBook");
    read_rels(x->zip, part, &r);
    it = eb ? rel_by_id(&r, rel_attr(eb, "id")) : NULL;
    if (it) {
        const char *p, *start = it->target;
        for (p = it->target; *p; p++)
            if (*p == '/' || *p == '\\' || *p == ':')
                start = p + 1;
        if (*start)
            name = copy(x, start, strlen(start));
    }
    free_rels(&r);
    owf_xml_free(xml);
    return name;
}

static void read_external(xlsx *x, const owf_xml_node *refs)
{
    const owf_xml_node *n;
    int count = count_children(refs, "externalReference"), i = 0;
    if (!count)
        return;
    x->books = calloc((size_t)count, sizeof *x->books);
    if (!x->books) {
        x->failed = 1;
        return;
    }
    x->nbooks = count;
    for (n = main_child(refs, "externalReference"); n && i < count; n = main_next(n, "externalReference"), i++) {
        char part[PART_NAME];
        const rel *it = rel_by_id(&x->wb_rels, rel_attr(n, "id"));
        if (rel_part(&x->wb_rels, it, part, sizeof part))
            x->books[i] = external_book(x, part);
    }
}

static void read_names(xlsx *x, const owf_xml_node *names)
{
    const owf_xml_node *n;
    for (n = main_child(names, "definedName"); n && !x->failed; n = main_next(n, "definedName")) {
        const char *name = attr(n, "name");
        long local = xlong(attr(n, "localSheetId"), -1, 0, 0xFFFF);
        int sheet = -1;
        owf_buf text;
        char *formula;
        const owf_xml_node *t;
        if (!name || !*name)
            continue;
        if (!strncmp(name, "_xlnm.", 6))
            name += 6;                   /* _xlnm.Print_Area: Print_Area, as the model has it */
        if (local >= 0 && local < x->nsheets)
            sheet = x->sheets[local].sheet;
        if (local >= 0 && sheet < 0)
            continue;                    /* a chart sheet's */
        owf_buf_init(&text);
        for (t = n->first; t; t = t->next)
            if (t->type == OWF_XML_TEXT)
                owf_buf_put(&text, t->name, t->length);
        owf_buf_putc(&text, 0);
        if (text.failed) {
            x->failed = 1;
            owf_buf_free(&text);
            return;
        }
        formula = owf_xl_formula_from_file((const char *)text.data, x->books, x->nbooks);
        owf_buf_free(&text);
        if (!formula) {
            x->failed = 1;
            return;
        }
        if (owf_book_add_name(x->book, name, sheet, formula, xbool(attr(n, "hidden"), 0)) < 0)
            x->failed = 1;
        free(formula);
    }
}

static int read_workbook(xlsx *x)
{
    owf_xml *xml;
    const owf_xml_node *root, *n, *sheets;
    const rel *it;
    char part[PART_NAME];
    int error = OWF_ERR_CORRUPT, i, count, active = 0;

    xml = read_xml(x->zip, x->wb_part, &error);
    if (!xml)
        return error == OWF_ERR_FORMAT ? OWF_ERR_CORRUPT : error;
    root = owf_xml_root(xml);
    if (!main_is(root, "workbook")) {
        owf_xml_free(xml);
        return OWF_ERR_FORMAT;
    }
    read_rels(x->zip, x->wb_part, &x->wb_rels);
    if (!strcmp(root->ns, NS_MAIN_STRICT))
        owf_report_add(x->report, OWF_NOTE_INFO, "This workbook is in Office Open XML's strict form");
    x->book->date1904 = xbool(attr(main_child(root, "workbookPr"), "date1904"), 0);
    active = (int)xlong(attr(main_child(main_child(root, "bookViews"), "workbookView"), "activeTab"), 0, 0, 0xFFFF);

    memcpy(x->theme, office_theme, sizeof x->theme);
    memcpy(x->palette, owf_xl_palette, sizeof x->palette);
    if (rel_part(&x->wb_rels, rel_by_type(&x->wb_rels, "/theme"), part, sizeof part))
        read_theme(x, part);
    if (rel_part(&x->wb_rels, rel_by_type(&x->wb_rels, "/styles"), part, sizeof part))
        read_styles(x, part);
    if (rel_part(&x->wb_rels, rel_by_type(&x->wb_rels, "/sharedStrings"), part, sizeof part))
        read_shared_strings(x, part);
    if ((it = rel_by_type(&x->wb_rels, "/vbaProject")) != NULL)
        owf_report_add(x->report, OWF_NOTE_LOST, "Macros (Visual Basic) are not read; the workbook opens without them");
    read_external(x, main_child(root, "externalReferences"));
    if (main_child(root, "pivotCaches"))
        owf_report_add(x->report, OWF_NOTE_LOST, "Pivot tables keep their cells; the pivot tables themselves are not read yet");

    /* the sheets, in the book's order */
    sheets = main_child(root, "sheets");
    count = count_children(sheets, "sheet");
    x->sheets = calloc((size_t)count + 1, sizeof *x->sheets);
    if (!x->sheets) {
        owf_xml_free(xml);
        return OWF_ERR_MEMORY;
    }
    for (n = main_child(sheets, "sheet"); n && !x->failed; n = main_next(n, "sheet")) {
        xsheet *xs = &x->sheets[x->nsheets];
        const char *name = attr(n, "name"), *state = attr(n, "state");
        it = rel_by_id(&x->wb_rels, rel_attr(n, "id"));
        xs->sheet = -1;
        xs->state = !state || !strcmp(state, "visible") ? 0 : !strcmp(state, "veryHidden") ? 2 : 1;
        xs->kind = !it ? 2 : type_is(it->type, "/worksheet") ? 0 : type_is(it->type, "/chartsheet") ? 1 : 2;
        x->nsheets++;
        if (xs->kind == 1) {
            owf_report_add(x->report, OWF_NOTE_LOST, "Chart sheets are not read yet");
            continue;
        }
        if (xs->kind == 2) {
            if (it && (type_is(it->type, "/xlMacrosheet") || type_is(it->type, "/xlIntlMacrosheet")))
                owf_report_add(x->report, OWF_NOTE_LOST, "Excel 4 macro sheets are not read");
            else if (it && type_is(it->type, "/dialogsheet"))
                owf_report_add(x->report, OWF_NOTE_LOST, "Dialog sheets are not read");
            else
                owf_report_add(x->report, OWF_NOTE_LOST, "A sheet could not be found in the file");
            continue;
        }
        if (!rel_part(&x->wb_rels, it, xs->part, sizeof xs->part)) {
            owf_report_add(x->report, OWF_NOTE_LOST, "A sheet could not be found in the file");
            continue;
        }
        {
            owf_buf clean;
            owf_sheet *sheet;
            owf_buf_init(&clean);
            owf_buf_put_utf8_clean(&clean, name ? name : "", name ? strlen(name) : 0);
            owf_buf_putc(&clean, 0);
            sheet = clean.failed ? NULL : owf_book_add_sheet(x->book, (const char *)clean.data);
            owf_buf_free(&clean);
            if (!sheet) {
                x->failed = 1;
                break;
            }
            sheet->hidden = xs->state;
            xs->sheet = x->book->nsheets - 1;
        }
    }
    read_names(x, main_child(root, "definedNames"));
    owf_xml_free(xml);
    for (i = 0; i < x->nsheets && !x->failed; i++)
        if (x->sheets[i].sheet >= 0)
            read_worksheet(x, &x->sheets[i]);
    if (active < x->nsheets && x->sheets[active].sheet >= 0)
        x->book->active = x->sheets[active].sheet;
    if (!x->book->nsheets)
        owf_report_add(x->report, OWF_NOTE_INFO, "The workbook has no worksheets");
    if (x->damaged)
        owf_report_add(x->report, OWF_NOTE_LOST, "The file is damaged or cut short; what could be read was read");
    return x->failed ? OWF_ERR_MEMORY : OWF_OK;
}

static void free_reader(xlsx *x)
{
    int i;
    free_rels(&x->wb_rels);
    free(x->fonts);
    free(x->fills);
    free(x->fill_approx);
    free(x->borders);
    free(x->numfmts);
    free(x->xf_style);
    free(x->dxfs);
    owf_buf_free(&x->sst);
    free(x->sst_at);
    for (i = 0; i < x->nbooks; i++)
        free(x->books[i]);
    free(x->books);
    free(x->sheets);
    clear_sheet_state(x);
    free(x->shared);
    free(x->pending);
    free(x->arrays);
}

/* The main part: through _rels/.rels, as Excel finds it. */
static int main_part(const owf_unzip *zip, char *out, size_t size)
{
    rels r;
    const rel *it;
    int ok;
    read_rels(zip, "_rels/.rels", &r);
    it = rel_by_type(&r, "/officeDocument");
    ok = it && rel_part(&r, it, out, size);
    free_rels(&r);
    if (!ok && owf_unzip_has(zip, "xl/workbook.xml")) {
        snprintf(out, size, "xl/workbook.xml");
        ok = 1;
    }
    return ok;
}

static int import_xlsx(const unsigned char *data, size_t length, owf_book *book, owf_report *report)
{
    owf_unzip zip;
    xlsx *x;
    int result;

    if (!owf_unzip_open(&zip, data, length))
        return OWF_ERR_FORMAT;
    if (owf_ooxml_kind(data, length) == OWF_PACKAGE_SHEET_BINARY) {
        owf_report_add(report, OWF_NOTE_LOST, "This is an Excel binary workbook (.xlsb); those cannot be read yet");
        return OWF_ERR_UNSUPPORTED;
    }
    x = calloc(1, sizeof *x);
    if (!x)
        return OWF_ERR_MEMORY;
    x->book = book;
    x->report = report;
    x->zip = &zip;
    x->array_budget = MAX_ARRAY_CELLS;
    x->text_budget = TEXT_BUDGET;
    owf_buf_init(&x->sst);
    if (!main_part(&zip, x->wb_part, sizeof x->wb_part))
        result = OWF_ERR_FORMAT;
    else
        result = read_workbook(x);
    free_reader(x);
    free(x);
    return result;
}

/* ---- Telling the files apart ---- */

static int detect_xlsx(const unsigned char *data, size_t length)
{
    int kind = owf_ooxml_kind(data, length);
    return kind == OWF_PACKAGE_SHEET || kind == OWF_PACKAGE_SHEET_MACROS ? 95 : kind == OWF_PACKAGE_SHEET_BINARY ? 60 : 0;
}

static int detect_xlsm(const unsigned char *data, size_t length)
{
    return owf_ooxml_kind(data, length) == OWF_PACKAGE_SHEET_MACROS ? 100 : 0;
}

const owf_book_format owf_book_format_xlsx = {
    "xlsx", "Excel workbook (Office Open XML)", "xlsx", detect_xlsx, import_xlsx, owf_export_xlsx
};

/* Macro-enabled workbooks (read without their macros) and templates: read
 * only, so that a file saved under one of these names never lacks what the
 * name promises. */
const owf_book_format owf_book_format_xlsm = {
    "xlsm", "Excel macro-enabled workbook or template (read only)", "xlsm xltm xltx", detect_xlsm, import_xlsx, NULL
};
